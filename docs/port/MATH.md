# Maths and floating point

How the port gets the PS2's numbers out of a host CPU: the FP mode the simulation runs in, the
hand-written models of the [EE](ARCHITECTURE.md#terms) FPU's and [VU0](ARCHITECTURE.md#terms)'s
arithmetic units, the integer model of the EE's soft `double`, the host copies of the newlib
functions the game linked, the VU0 routines written in C, and the tests and build checks that hold
all of it in place.

## Files

| path | what it holds |
| --- | --- |
| `port/platform/fpenv.c`, `fpenv.h` | the simulation's FP mode and the host's (MXCSR, FPCR) |
| `port/platform/fpenv_test.c` | the FP mode test (`fpenv`, `fpenv_trap`) |
| `port/math/ps2float.h`, `ps2float.c` | the PS2 float models: divider, multiplier, adder, square roots, conversions |
| `port/math/softdouble.h`, `softdouble.c` | the EE's `double` arithmetic in integers (libgcc's soft float) |
| `port/math/newlib/ico_newlib.h` | the host copies of the newlib functions the game calls |
| `port/math/newlib/ico_libm.c` | `ico_atan2f`, `ico_acosf`, `ico_asinf`, `ico_sinf`, `ico_cosf`, `ico_fmodf`, `ico_sqrtf` |
| `port/math/newlib/qsort.c`, `rand.c` | `ico_qsort`; `ico_rand`, `ico_srand`, `ico_rand_state` |
| `port/compat/ico_libc.h` | which C library calls of the game go to the copies and which stay the host's |
| `port/math/ico_math.h` | the current matrix, the VU0 register save, the matrix helpers, the R register |
| `port/math/vector_inline.h` | the small vector routines behind the game's inline VU0 helpers |
| `port/math/libvu0.c` | Sony's libvu0 (`sceVu0*`) in C |
| `port/math/matrix.c` | `seki/src/Matrix.c`'s vector and matrix routines and the R register |
| `port/math/matrix_stack.c` | the current-matrix routines, `ico_apply_matrix`, `ico_apply_matrix_ps2` |
| `port/math/matrix_drive.c` | `sugipon/src/matrixDrive.c`'s VU0 and quadword-copy routines |
| `port/math/quaternion.c` | `sugipon/src/quaternion.c`'s VU0 routines |
| `port/math/cloth.c` | the VU0 routines `sugipon/src/clothAnimation.c` defines with external linkage |
| `port/math/CMakeLists.txt` | the `ico_math` library, the tests, the soft-float oracle, `softdouble_gate` |
| `port/math/test/` | `math_test.c`, `newlib_test.c`, `softdouble_test.c`, `dpbit_harness.cmake` |
| `tools/softdouble_gate.py` | the gate on the game functions that do `double` arithmetic |
| `tools/check_android_flags.sh` | the Android build's compile options and the no-FMA object check |
| `cmake/IcoFlags.cmake` | `ICO_SEMANTIC_OPTIONS`: `-ffp-contract=off` and the other semantics options |

## The FP environment of the simulation

The EE's FPU rounds every single-precision result toward zero and flushes denormal inputs and
outputs to zero. `ico_fpenv_sim_enter` puts the host FPU in the closest IEEE mode: on x86-64 MXCSR
gets round toward zero, FTZ and DAZ over the power-on value `0x1F80`; on arm64 FPCR gets RMode
toward zero and FZ. `ico_fpenv_host_enter` puts back the host defaults, round to nearest with
denormals kept and every exception masked: on x86-64 it loads MXCSR's power-on value, which also
clears the sticky flags, and on arm64 it clears FPCR's rounding, flush and trap bits and leaves
FPSR as it is (`port/platform/fpenv.c` (`ico_fpenv_sim_enter`, `ico_fpenv_host_enter`)). Any
other architecture is a compile error. `ico_fpenv_raw` returns the control register for tests and
[trace](ARCHITECTURE.md#terms) headers (`port/platform/fpenv.h`).

The mode does not give the EE's other differences: the EE has no infinities or NaNs, clamps
results, and has its own division and square root. Those are the models of
`port/math/ps2float.h` (below), which the sites listed below call (`port/platform/fpenv.h`).

Where each mode is entered:

-  `ico_host_init` enters the simulation mode first, and installs `ico_fpenv_sim_enter` as the
  scheduler's [fiber](ARCHITECTURE.md#terms) start hook (`port/platform/host_loop.c`
  (`ico_host_init`); `port/platform/sched.h` (`ico_sched_set_fiber_start_hook`)). The fiber switch
  does not save MXCSR or FPCR, and minicoro's Windows fiber path gives each
  [fiber](ARCHITECTURE.md#terms) an FP state of its own, so every fiber sets the mode at its top
  (`port/platform/fiber.h`).
- `ico_host_step` enters the simulation mode at the start of each [vsync](ARCHITECTURE.md#terms)
  step and again after the audio push, which is an SDL call (`port/platform/host_loop.c`
  (`ico_host_step`)).
- `ico_sched_call_on_host` runs its host function in the host mode and puts the simulation mode
  back afterwards, whatever the function left in the register (`port/platform/sched.c`
  (`run_host_call`)).
- In the [window build](ARCHITECTURE.md#terms) the main loop enters the host mode before it pumps
  the window, SDL and the renderer; the next `ico_host_step` puts the simulation mode back
  (`port/platform/main_host.c` (the `ico_window_pump` call)).

Built with `ICO_FPTRAP` (the `fptrap` CMake preset), the simulation mode also unmasks the
divide-by-zero and invalid exceptions, so the first such operation stops the program at the
instruction that raised it: SIGFPE on Linux, a structured exception on Windows
(`port/platform/fpenv.c` (`ico_fpenv_sim_enter`); `CMakeLists.txt` (`ICO_FPTRAP_DEFINITION`)). The
crash handlers report it like any other fault (`port/platform/diag_host.c` (`fatal_code`)). The
models below are written so that they raise nothing a branch would have skipped: `ps2_ftoi`
brings every lane in range before its one conversion, because a vectorised convert raises
invalid on all lanes (`port/math/ps2float.h` (`ps2_ftoi`)).

`fpenv_test` checks the two modes bit for bit: in the simulation mode 1/3 is `0x3EAAAAAA`,
`FLT_MIN * 0.5` is zero and a denormal input reads as zero; in the host mode 1/3 is `0x3EAAAAAB`
and the denormals survive. With `ICO_FPTRAP`, `fpenv_test trap` checks that 1/0 in the simulation
mode raises SIGFPE (`port/platform/fpenv_test.c` (`main`)). `fiber_test` checks that a fiber
whose entry sets the simulation mode sees the host thread's register and leaves it as it was
(`port/platform/test/fiber_test.c` (`test_fp_state`)); `sched_test` checks that a host call runs in
the host mode and that the calling thread is back in the simulation mode after it
(`port/platform/test/sched_test.c` (`test_call_on_host`)).

## The PS2 float models

Ordinary game arithmetic stays plain C float arithmetic in the simulation mode, compiled with
`-ffp-contract=off`, so `a * b + c` rounds the product and then the sum as VU0's multiply-add does
(`port/math/ps2float.h` (header comment)). What plain C gets wrong is written out in
`port/math/ps2float.h` and `port/math/ps2float.c`. The divider, multiplier, adder and square root
are PCSX2 PR #12001's hardware-derived `PS2Float` model; the divider and the square root are
radix-2 SRT recurrences on a carry-save remainder that come out at the truncated result or one ulp
above it, in integer arithmetic only, so the host's rounding mode does not reach them
(`port/math/ps2float.c` (header comment)).

Every function in `port/math/ps2float.h`:

| function | what it models |
| --- | --- |
| `ps2_float_bits`, `ps2_bits_float` | a float's bit pattern and back, with no arithmetic |
| `ps2_is_zero` | a zero or a denormal, which the PS2 reads as zero; tests the exponent bits, so it holds where DAZ is off |
| `ps2_fmax_signed` | +-Fmax (`0x7F7FFFFF`) with the sign of a XOR b, the PS2's result of a division by zero |
| `ps2_operand` | an operand as the FPU and VU0 read it: an exponent-255 pattern (the allocator's `0xFFFFFFFF` fill of freed blocks) is +-Fmax, not Inf or NaN |
| `ps2_div_nonzero` | the divider's quotient of two nonzero operands (`ps2float.c`) |
| `ps2_div` | VU0 `vdiv` and EE `div.s`: a zero divisor gives +-Fmax, 0/0 included; a zero dividend a signed zero; else the divider's quotient, neither IEEE round toward zero nor round to nearest |
| `ps2_mul_mantissa` | the 24 x 24-bit mantissa product as the multiplier forms it: radix-4 Booth partial products in a carry-save tree whose low 15 bits are dropped, so the product can be one unit of bit 15 low, and `1.0 * x` can differ from `x * 1.0` |
| `ps2_mul` | EE `mul.s` and the product of VU0 `vmul`/`vmadd`/`vmsub`, truncated to 24 bits; not commutative, so callers pass the instruction's operand order (fs, ft) |
| `ps2_add_aligned` | the adder's sum once the smaller operand is aligned: six extra low bits, truncated; an operand 25 or more binades below the other drops |
| `ps2_add`, `ps2_sub` | the EE adder: the smaller operand keeps one bit below its alignment shift, so `1.0f - 2^-30` is `0x3F800000` where IEEE round toward zero gives `0x3F7FFFFF` |
| `ps2_sqrt` | VU0 `vsqrt`: sqrt(\|x\|), never NaN (`ps2float.c`) |
| `ps2_rsqrt` | VU0 `vrsqrt`: a / sqrt(\|b\|) as `ps2_div` of `ps2_sqrt`, two roundings; b zero gives +-Fmax, or a signed zero when a is zero too (PCSX2's value, not checked on hardware) (`ps2float.c`) |
| `ps2_ftoi` | EE `cvt.w.s` and VU0 `vftoi0`: truncates and saturates at +-2^31 by sign, an exponent-255 pattern included, where x86's `cvttss2si` gives `0x80000000` for both signs |
| `ps2_ftoi4` | VU0 `vftoi4`: 28.4 fixed point, `ps2_ftoi(x * 16)` |
| `ps2_itof` | VU0 `vitof0`: a plain conversion, rounded toward zero by the simulation mode |
| `ps2_max`, `ps2_min` | VU0 `vmax`, `vmini` on one field: an ordinary compare (no NaN to order) |

Overflow needs no model in plain arithmetic: under round toward zero an overflowing result is
already `FLT_MAX`, the port's Fmax; the hardware's `0x7FFFFFFF` is not a host float
(`port/math/ps2float.h` (header comment)).

### Where the models are used

The game sources call the models from their host bodies, each where the plain operator gives a
different result the game can reach: a zero divisor, an exponent-255 operand, an out-of-range
conversion, or a last bit that decides a branch. By function:

- `ps2_div` (the most used): the motion manager's leg and arm IK, where a zero distance or bone
  length divides by zero (`ico2/sugipon/src/motMan_getFinalMatrix.c.inc` (`_getFinalMatrix`,
  `limitHPAngleAndSetB`)); `motMan_rootUpdate.c.inc` (`rootUpdateDirectPlay`), `motionManager.c`
  (`checkWallState`), `motionOrientManager.c` (`getMotionGeometry`), `handManager.c`
  (`connectToTarget`), `enemy.c` (`dispEnemyObject`), `weapon.c` (`initializeQueenzSword`),
  `particleEffect.c` (`_setParticleEffect`, `particleEffectOffScreen`), `staticBlur.c` (`calcSun`),
  `darkVolume.c` (`projectVertex`) and `lineManager.c` (`perspLine`, `clipAtX`, `clipAtY`), all
  under `ico2/sugipon/src`; the camera (`ico2/omori/src/camera-root.c` (`SetCameraMatrix`)); the
  projections of `ico2/seki/src/GifPacket.c` (`rotTransPers`), `GsBase.c` (`VS_DIV`) and
  `Shadow.c` (`clipVolumeEdge`); lightning (`ico2/ito/src/lightning.c` (`set_vertex`,
  `DrawLightning2`)); the collision (`ico2/fumi/src/fieldCollision.c` (`FC_DIV`)). In `port/`:
  libvu0's normalise, divide and projection (`port/math/libvu0.c` (`sceVu0Normalize`,
  `sceVu0DivVector`, `sceVu0RotTransPers`)), `_RotTransPersCurrentMatrix`
  (`port/math/matrix_stack.c`), `ico_domain_error` (`port/math/newlib/ico_libm.c`) and the
  [VU1](ARCHITECTURE.md#terms) CPU references (`port/render/vu1_ref/vu1_ref_internal.h`
  (`vu_rcp`); `port/render/vu1_ref/vu1_scissor.c` (`interpolate`)).
- `ps2_ftoi`: `ico2/fumi/src/commonact.c` (`_ACTCorrectMsg`), `fieldCollision.c` (`FC_FTOI`),
  `ico2/omori/src/chain.c` (`ChainNodeSpan`), `ico2/sugipon/src/enemy.c` (`dispEnemyObject`),
  `ico2/ito/src/lightning.c` (`lightningSeg`, `DrawLightning2`); `port/math/libvu0.c`
  (`sceVu0FTOI0Vector`, `sceVu0RotTransPers`), `port/math/matrix.c` (`_FTOI0Vector`) and
  `vu1_ref_internal.h` (`vu_ftoi0_4`).
- `ps2_ftoi4`: `GifPacket.c` (`rotTransPers`), `Shadow.c` (`storeVolumeVerts`), `darkVolume.c`
  (`projectVertex`); `libvu0.c`, `matrix.c` (`_FTOI4Vector`), `matrix_stack.c`
  (`_RotTransCurrentMatrix`) and the VU1 references (`vu1_ref_internal.h` (`vu_xyz`);
  `vu1_normal.c`, `vu1_particle.c`, `vu1_scissor.c`).
- `ps2_mul`, `ps2_add`, `ps2_sub`: the collision rays (`ico2/fumi/src/fieldCollision.c`
  (`FC_MUL`, `FC_ADD`, `FC_SUB`)), where IEEE round toward zero puts some hits one ulp off the EE's
  and, where two coplanar floors meet, picks the other floor; `ico_apply_matrix_ps2`
  (`port/math/matrix_stack.c`); and, for `ps2_add` and `ps2_sub`, the libm copies
  (`port/math/newlib/ico_libm.c` (below)).
- `ps2_operand`: `ico_apply_matrix` and `ico_apply_matrix_ps2` read a vector's w through it
  (`port/math/matrix_stack.c`); `ico2/sugipon/src/motionOrientManager.c` (`getMotionGeometry`),
  `enemyParts.c` (`UpdatePointBlur`) and `ico_domain_error` (`ico_libm.c`).
- `ps2_sqrt`: `port/math/matrix.c` (`_Sqrt`, `_GetNorm`, `_GetLength`, `_GetLengthXY`,
  `_GetLengthXZ`),
  `port/math/matrix_drive.c` (`FSqrt`, the lengths), `port/math/libvu0.c` (`sceVu0Normalize`,
  `ecossin`), `port/math/cloth.c` (`getXZLength`), `ico2/sugipon/src/motionManager2.c`
  (`motSqrtStart`).
- `ps2_rsqrt`: `port/math/matrix.c` (`_NormalizeVector`, `_MakeNormalLightMatrix`,
  `_RemakeNormal`'s helpers),
  `port/math/vector_inline.h` (`ico_sub_inv_length`), `port/math/cloth.c` (`getXZInvLength`),
  `ico2/sugipon/src/clothAnimation.c` (`fSqrtInv_i`, `xzInvLength_i`) and the VU1 reference
  (`port/render/vu1_ref/vu1_normal.c` (`normalRef`)).
- `ps2_min`, `ps2_max`: `ico2/sugipon/src/darkVolume.c` (`projectVertex`), `port/math/libvu0.c`
  (`sceVu0ClampVector`), `vu1_ref_internal.h` (`vu_max0`, `vu_min255`).
- `ps2_itof`: `port/math/libvu0.c` (`sceVu0ITOF0Vector`, `sceVu0ITOF4Vector`). `ps2_is_zero`:
  `port/render/vu1_ref/vu1_particle.c` (`vu1ref_particle`). `ps2_float_bits` and `ps2_bits_float`:
  the R register (`port/math/matrix.c` (below)).

`port/math/test/math_test.c` calls every model directly. A site not in this list does plain IEEE
arithmetic in the simulation mode. A new site that calls a model is added to this list;
`math_test` tests the models, not their callers.

### What stays plain IEEE

- Every game addition, subtraction and multiplication outside the collision rays and the libm
  copies. `ico_apply_matrix_ps2` costs about 75 times `ico_apply_matrix`, so only the collision
  rays use it (`port/math/ico_math.h` (`ico_apply_matrix_ps2`)).
- Every division outside the sites listed above, the libm copies' divisions included: those use the
  host's IEEE division under the simulation's rounding mode (`port/math/newlib/ico_libm.c`
  (header comment)).
- Expressions of constants: ee-gcc folded most of them at compile time in round to nearest, and a
  host compiler may or may not fold the same ones. Inside the libm copies each folded constant
  is written as the literal the EE object holds; elsewhere the compiler decides
  (`port/math/newlib/ico_libm.c` (header comment)).
- The FP mode off the simulation thread: other threads (the heartbeat and watchdog thread,
  SDL's own) run in the host mode, so a model that must hold everywhere tests the exponent bits
  rather than trusting DAZ (`port/math/ps2float.h` (`ps2_is_zero`)).

## The soft double

The EE has no `double` hardware: ee-gcc turned every `double` operation into a call to libgcc's
soft float (`sce/libgcc/dp-bit.c`, `fp-bit.c`), which rounds to nearest even, reads a denormal
operand as zero and truncates a tiny result into a denormal. On the host those expressions would
be SSE or NEON doubles in the simulation's round-toward-zero mode. `port/math/softdouble.h`
computes the EE's results with integers only, on bit patterns (`uint64_t` for a double):
`ico_dadd`, `ico_dsub`, `ico_dmul`, `ico_ddiv`, `ico_dcmp`, the conversions `ico_i2d`, `ico_d2i`,
`ico_d2f`, `ico_f2d` and their `_bits` forms, `ico_dbits` and `ico_dval` to move bits through a
variadic argument, and `ICO_D(lit)` for the bits of a `double` literal (`port/math/softdouble.h`).
`port/math/softdouble.c` lists each way the library differs from a textbook IEEE implementation
and repeats it: the 61-bit significand with eight guard bits, the sticky bit, the specials and the
comparison's answer for a NaN (`port/math/softdouble.c` (header comment)).

The game functions that did `double` arithmetic (33 functions in 20 files, and two static inline
helpers expanded into them) call these. `tools/softdouble_gate.py` holds the list (`FUNCTIONS`)
and fails when one of them holds a `double` keyword (other than `va_arg(ap, double)`) or an
unsuffixed floating constant outside `ICO_D(...)`; with `--build` it also compiles the files with
`-Wdouble-promotion -Wfloat-conversion` and fails on a warning inside one of the functions
(`tools/softdouble_gate.py` (docstring)). It runs as the `softdouble_gate` test on native builds
with `compile_commands.json` (`port/math/CMakeLists.txt`).

## The newlib copies

The results of the C library functions the game calls differ between C libraries: glibc and the
Windows CRTs have other random generators, `qsort` leaves the order of equal elements open, and
host libms differ from newlib's fdlibm code in the last bits and in the EE's round-toward-zero
mode. Those results feed game state (a random stream, the order of sorted objects, an angle that
decides a branch), so the port carries host copies of the members the game linked
(`port/compat/ico_libc.h` (header comment)). `port/compat/math.h` and `port/compat/stdlib.h` stand
in for the host headers on the game's include path and, under `ICO_HOST`, map `rand`, `srand`,
`qsort`, `RAND_MAX`, `atan2f`, `acosf`, `asinf`, `sinf`, `cosf`, `fmodf` and `sqrtf` onto the
`ico_*` names; code outside the game's include path keeps the host functions
(`port/compat/ico_libc.h`). The double `sqrt` and `floor`, the formatting and parsing calls and
the string calls stay the host's, with their differences listed there.

- `ico_rand`, `ico_srand`: newlib's 32-bit LCG with the state word initialised to 1, as
  `_REENT_INIT` does; `ICO_RAND_MAX` is newlib's `0x7FFFFFFF` (`port/math/newlib/rand.c`;
  `port/math/newlib/ico_newlib.h`). `ico_rand_state` reads the state word for diagnostics.
- `ico_qsort`: newlib's 4.4BSD qsort with the EE's sequence of comparisons and exchanges, so
  elements that compare equal end in the same order (`port/math/newlib/qsort.c` (header comment)).
- `ico_atan2f`, `ico_acosf`, `ico_asinf`, `ico_sinf`, `ico_cosf`, `ico_fmodf`, `ico_sqrtf`:
  fdlibm's single-precision code as newlib carries it, with newlib's X/Open wrappers, each section
  naming the `libm.a` member it was copied from (`port/math/newlib/ico_libm.c` (header comment)).
  Nothing in the file calls the host libm, and only the `ico_*` entry points are external.

What `ico_libm.c` keeps from the EE copies: every operation, its order, its operand types (float
throughout) and every constant. What it changes without changing a result: shifts that can reach
the sign bit are done on `uint32_t`, and the wrappers return the X/Open results without a
`struct exception`, `matherr` or `errno`. What it changes to give the EE's result: every float
addition and subtraction in `sinf`'s kernels and argument reduction, `atanf`, `atan2f`, `acosf`
and `asinf` goes through `ps2_add`/`ps2_sub`; the case that shows it is `sinf`'s table argument
just under pi/2, where `1 - 4.7e-9` is 1.0 on the EE and `0x3F7FFFFF` under IEEE round toward
zero (`port/math/newlib/ico_libm.c` (header comment)).

What it does not reproduce: `__kernel_rem_pio2f` and `floorf` (used only for `sinf` arguments
above 2^7 * pi/2, which the game never passes) and `fmodf` (exact) stay IEEE; divisions are the
host's IEEE division; the EE's lack of infinities and NaNs is reproduced only in `acosf`'s and
`asinf`'s domain errors, where `ico_domain_error` gives the EE's +Fmax for an exponent-255
argument instead of the host's NaN (`port/math/newlib/ico_libm.c` (`ico_domain_error`)).
`ico_cosf` follows newlib's `sf_cos.c` and is not checked against an EE object, because the game's
link holds no `cosf`; `ico_sqrtf` is the `__ieee754_sqrtf` core alone, because the EE build links
no `sqrtf` wrapper (`port/math/newlib/ico_libm.c` (`ico_cosf`, `ico_sqrtf`)). The file refuses to
compile where float maths has excess precision (`__FLT_EVAL_METHOD__` other than 0).

## VU0 in C

The game used VU0 in macro mode for vector and matrix maths, through Sony's libvu0 and its own
inline assembly. On the host every such routine is C over plain floats, with the products and
sums in the assembly's order, the multiply-add pairs unfused, and the fields the assembly leaves
alone left alone. `ico2/common/include/typedef.h` includes `port/math/ico_math.h`, which includes
`ps2float.h`, `softdouble.h` and `vector_inline.h`, so every game source sees them.

- `port/math/libvu0.c`: the `sceVu0*` entry points of `port/compat/libvu0.h`, written from the
  instruction sequences of `sce/libvu0/libvu0.c`. On the PS2 they used vf4-vf9, the registers
  that also hold the current matrix; on the host they touch only their arguments. Matrix products
  go through `ico_apply_matrix`; the rotations take sine and cosine from the libvu0 polynomial
  (`ecossin`); `sceVu0Normalize` is `vsqrt` then a `vdiv` of 1, two roundings
  (`port/math/libvu0.c`).
- `port/math/vector_inline.h`: the static inline helpers that `sugipon/include/sugiCommon.h` and
  `clothAnimation.c` spell in VU0 assembly (`ico_plane_distance`, `ico_distance_squared`,
  `ico_distance_squared_xz`, `ico_xz_length_square`, `ico_sub_inv_length`, `ico_scale_add_xyz`,
  `ico_scale_xz`), called from their `ICO_HOST` bodies; sums run x, then y, then z
  (`port/math/vector_inline.h`).
- `port/math/ico_math.h`: what the game kept in VU0 registers between calls and the shared
  helpers: `ico_current_matrix`, `ico_vu0_registers_push`/`_pop`, `ico_apply_matrix`,
  `ico_apply_matrix_w1`, `ico_apply_matrix_ps2`, `ico_set_transpose_matrix_ps2`,
  `ico_quaternion_rotation_rows`, `ico_quaternion_norm2`, `ico_vu0_random_set`/`_get`.

## Matrices and the R register

`port/math/matrix.c` holds the routines of `seki/src/Matrix.c` that do not touch the current
matrix: vector sums, scales and products (`cross3` pairs the products as `vopmula`/`vopmsub` do),
lengths, `_NormalizeVector` through `vrsqrt`, `_MulMatrix`, `_InversMatrix`, the light matrices
and `_RemakeNormal`'s normal builders, which add z before y where the assembly does
(`port/math/matrix.c` (`sq_len_xzy`)). The game's `ico2/seki/src/Matrix.c` keeps its C routines and
calls these.

VU0's R register is a 23-bit LFSR read as a float in [1, 2): the bits are
`0x3F800000 | state`. One advance takes bits 4 and 22, shifts the state left one and XORs the two
taps into bit 0. The polynomial is PCSX2's and is unverified on hardware (`port/math/matrix.c`
(`r_next`)). `_InitRandom` is `vrinit` with seed + 1 then `vrxor` with seed + seed; `_GetRandom` is
one advance minus 1.0, exactly `(R & 0x7FFFFF) / 2^23`; `_GetRandomVector` advances three times
and `_GetRandomVector0` once for all three fields, and neither writes w (both have no caller)
(`port/math/matrix.c`). `ico2/ito/src/lightning.c` writes R directly on the PS2 (`ctc2` to
`$vi20`); its host form is `ico_vu0_random_set`, which keeps the low 23 bits
(`port/math/ico_math.h` (`ico_vu0_random_set`)). `math_test` checks the seed, the state after
`vrinit` and `vrxor` and six steps against fixed values (`port/math/test/math_test.c`
(`test_random`)).

## The current matrix

The PS2 kept the current matrix in VU0 registers vf4-vf7 and pushed it to VU0 data memory through
vi15. On the host it is `ico_current_matrix` and a 64-entry stack, which wraps as VU0's 4 KB data
memory does (`port/math/matrix_stack.c` (`MATRIX_STACK_DEPTH`)). Every routine that combines the
current matrix with another (rotate, scale, translate, multiply) builds the other matrix in full,
zeros and ones included, and runs the four-term multiply-add on it, so the rounding and the order
of operations are the PS2's (`port/math/matrix_stack.c` (header comment)).

- `ico_apply_matrix` computes `m[0]*x + m[1]*y + m[2]*z + m[3]*w` left to right for all four
  fields, the `vmulax`/`vmadday`/`vmaddaz`/`vmaddw` sequence; w is read through `ps2_operand`,
  because callers often write only x, y and z and leave a stale word in w, which times a zero
  translation row is 0 on the PS2 and NaN on the host (`port/math/matrix_stack.c`
  (`ico_apply_matrix`)). `ico_apply_matrix_w1` takes w as 1.
- `ico_apply_matrix_ps2` is the same sequence with each step through `ps2_mul` and `ps2_add`,
  every product truncated before its sum, the matrix field the multiplicand (fs) and the vector
  field the broadcast (ft). The collision rays use it through `FC_APPLY`
  (`port/math/matrix_stack.c` (`ico_apply_matrix_ps2`); `ico2/fumi/src/fieldCollision.c`).
- `_RotTransPersCurrentMatrix` divides by w as `vdiv` then `vmulq`, two roundings rather than
  `x / w` (`port/math/matrix_stack.c`).
- `ico_vu0_registers_push`/`_pop` are the host halves of `_PushVu0Registers` and
  `_PopVu0Registers`. On the PS2 those save vf1-vf31 around code that clobbers them; on the host
  only the current matrix is left of those registers, so that is what is saved, up to eight deep
  (the game nests five and asserts at six) (`port/math/matrix_stack.c` (`VU0_SAVE_DEPTH`)).

`port/math/matrix_drive.c` holds `sugipon/src/matrixDrive.c`'s VU0 and quadword-copy routines:
the copies are `memmove`s, `FSqrt` is `ps2_sqrt`, and `ico_set_transpose_matrix_ps2` is
`MatrixDrive_SetTransposeMatrix` with its `sceVu0ApplyMatrix` as `ico_apply_matrix_ps2`, for the
collision's `FC_SET_TRANSPOSE` (`port/math/matrix_drive.c` (`ico_set_transpose_matrix_ps2`)).

## Quaternions and cloth

`port/math/quaternion.c` holds `sugipon/src/quaternion.c`'s VU0 routines, quaternions as
(x, y, z, w) with w the scalar part. `MultiQuaternion` forms the dot product and the cross terms
in the assembly's order; `ico_quaternion_rotation_rows` scales q by sqrt(2) first, as the
assembly's `quatToMatrixScale` table does, so the 2ab terms come out of single products, and
writes rows 0-2 only (`port/math/quaternion.c`). The game's `ico2/sugipon/src/quaternion.c` calls
it for its `GetMatrixFromQuaternion*` bodies.

`port/math/cloth.c` holds the VU0 routines `clothAnimation.c` defines with external linkage. None
has a caller outside that file, whose own code uses the static inline copies, but the symbols are
the game's. `FSqrtInv` is declared `void (void)` while its assembly computes a reciprocal square
root of `$f12`; with no argument there is nothing to compute, so the host body is empty
(`port/math/cloth.c` (`FSqrtInv`)).

## Compile options

`ICO_SEMANTIC_OPTIONS` (`cmake/IcoFlags.cmake`) compiles the game and `port/` against the EE's
semantics; the options and the reason for each are in
[GAME_CODE.md](GAME_CODE.md#semantics-options). `ico_math` and its tests are compiled with them and
the port warning set (`port/math/CMakeLists.txt`).

x86-64 at the default target has no FMA, so a contracted multiply-add would show only on arm64,
where clang contracts `a * b + c` into `fmadd` unless told not to. `tools/check_android_flags.sh`
checks the Android build: every `ico2/` and `port/` source in its `compile_commands.json` has
`-fno-strict-aliasing`, `-fwrapv`, `-ffp-contract=off`, `-fno-fast-math` and `-fsigned-char`
and none of the options that undo them (`-ffast-math`, `-Ofast`, `-ffp-contract=fast` or `on`,
`-fassociative-math`, `-freciprocal-math` and the rest); no game or `port/math` object holds an
`fmadd`, `fmsub`, `fnmadd` or `fnmsub` (llvm-objdump); and it runs the stack-guard and const-write
checks (`tools/check_android_flags.sh`). CI runs it on the Android job
(`.github/workflows/ci.yml`).

## Tests and gates

| test | what it checks |
| --- | --- |
| `math` (`port/math/test/math_test.c`) | the PS2 float models bit for bit (`test_helpers`), a stale w read as Fmax (`test_stale_w`), multiply-add not fused (`test_madd_not_fused`), the R register (`test_random`), matrices, rotations, vectors, inverse and projection, normals against double-precision references or exact results, and `ico_apply_matrix_ps2` (`test_apply_ps2`); runs in the simulation mode |
| `newlib` (`port/math/test/newlib_test.c`) | `ico_rand`'s sequence from the start-up seed, `ico_qsort`'s order of equal keys on a hand-traced input, libm values, in the host mode and in the simulation mode |
| `softdouble` (`port/math/test/softdouble_test.c`) | every soft-double function bit for bit against the EE's own `dp-bit.c` and `fp-bit.c` over a fixed-seed sweep and the conversion edges; not built on Windows (the oracle's bitfield unions need the GCC layout) |
| `port/math/test/dpbit_harness.cmake` | not a test: copies `sce/libgcc`'s soft float into the build for the `softdouble` oracle, giving the functions that returned through a register a host return type |
| `softdouble_gate` (`tools/softdouble_gate.py`) | the soft-double functions hold no host `double` arithmetic |
| `fpenv`, `fpenv_trap` (`port/platform/fpenv_test.c`) | the two FP modes; the trap under `ICO_FPTRAP` |
| `tools/check_android_flags.sh` | the Android compile options and no fused multiply-add in the objects |

The `fptrap` preset builds with `ICO_FPTRAP` and CI runs its tests, so a division by zero or an
invalid operation in the simulation's code under test stops the run at the instruction
(`CMakePresets.json` (`fptrap`); `.github/workflows/ci.yml`). The `math`, `newlib` and `softdouble`
tests carry the `game` label (`cmake/IcoTests.cmake`).

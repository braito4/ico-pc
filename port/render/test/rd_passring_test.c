/* rd_passring_test.c: the pass ring (port/render/rd_passring.c), pure.
 *
 *   order     entries come back oldest first with rising sequence numbers,
 *             the replay, frame, keep flag and list in force when pushed
 *   wrap      past RD_PASSRING_SIZE entries the ring holds the newest
 *             ones; the replay table still names the replay and frame of a
 *             sequence number the ring moved past, until it too moves on
 *   markers   the marker push returns is the entry's sequence number
 *   pipeline  only a render pass or exact blend takes the pipeline asked
 *             for; an upload batch takes its bytes
 *   format    a pass, a copy and the present as the loss report prints
 *             them */
#include "rd_passring.h"
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);               \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static const char *postName(int post)
{
    return post == 3 ? "fog" : "other";
}

static void testOrder(void)
{
    rd__passring_reset();
    CHECK(rd_passring_count() == 0);
    CHECK(rd_passring_get(0) == NULL);
    rd__passring_frame(316, 121, 0);
    const void *m0 = rd__passring_push(RD_PASSK_UPLOAD, 0, 0, 0, 0, 0, 0, 0, -1);
    rd__passring_bytes(2610u * 1024u);
    rd__passring_list(0);
    const void *m1 = rd__passring_push(RD_PASSK_PASS, 1, 1922, 1080, 2, 1922, 1080, 0, 3);
    rd__passring_pipeline(77, 4, 9, 0);
    rd__passring_list(12);
    rd__passring_push(RD_PASSK_PASS, 5, 1922, 1080, 0, 0, 0, 1, -1);
    CHECK((uintptr_t)m0 == 1 && (uintptr_t)m1 == 2);
    CHECK(rd_passring_count() == 3);
    const RdPassRec *a = rd_passring_get(0), *b = rd_passring_get(1), *c = rd_passring_get(2);
    CHECK(a && b && c);
    if (!a || !b || !c) {
        return;
    }
    CHECK(a->seq == 1 && a->kind == RD_PASSK_UPLOAD && a->bytes == 2610u * 1024u);
    CHECK(a->list == 255 && a->replay == 316 && a->frame == 121 && !a->keep);
    CHECK(a->pipeline == 0); /* an upload batch takes no pipeline */
    CHECK(b->seq == 2 && b->list == 0 && b->color == 1 && b->cw == 1922 && b->ch == 1080);
    CHECK(b->depth == 2 && b->pipeline == 77 && b->vs == 4 && b->fs == 9 && b->post == 3);
    CHECK(c->seq == 3 && c->list == 12 && c->storage == 1 && c->depth == 0 && c->pipeline == 0);
    /* the pipeline goes to the newest entry only while it is a pass */
    rd__passring_push(RD_PASSK_COPY, 1, 10, 10, 2, 10, 10, 0, -1);
    rd__passring_pipeline(99, 1, 1, 0);
    CHECK(rd_passring_get(3)->pipeline == 0);
    rd__passring_bytes(5); /* not an upload batch: ignored */
    CHECK(rd_passring_get(3)->bytes == 0);
    CHECK(rd_passring_replay_count() == 1);
    const RdPassReplay *r = rd_passring_replay_get(0);
    CHECK(r && r->replay == 316 && r->frame == 121 && r->first == 1 && r->last == 4);
}

static void testWrap(void)
{
    rd__passring_reset();
    /* 12 replays of 10 passes each, every third a keep frame */
    for (uint32_t rep = 1; rep <= 12; rep++) {
        rd__passring_frame(rep, 100 + rep, rep % 3 == 0);
        for (int p = 0; p < 10; p++) {
            rd__passring_list(p);
            rd__passring_push(RD_PASSK_PASS, (uint32_t)p + 1, 64, 64, 0, 0, 0, 0, -1);
        }
    }
    CHECK(rd_passring_count() == RD_PASSRING_SIZE);
    const RdPassRec *first = rd_passring_get(0);
    const RdPassRec *last = rd_passring_get(RD_PASSRING_SIZE - 1);
    CHECK(first && last && last->seq == 120 && first->seq == 120 - RD_PASSRING_SIZE + 1);
    CHECK(last && last->replay == 12 && last->frame == 112 && last->keep == 1 && last->list == 9);
    for (uint32_t i = 1; i < rd_passring_count(); i++) {
        CHECK(rd_passring_get(i)->seq == rd_passring_get(i - 1)->seq + 1);
    }
    CHECK(rd_passring_get(RD_PASSRING_SIZE) == NULL);
    /* the replay table: the last RD_PASSRING_REPLAYS replays */
    CHECK(rd_passring_replay_count() == RD_PASSRING_REPLAYS);
    const RdPassReplay *r = rd_passring_replay_of(50); /* replay 5's passes 41..50 */
    CHECK(r && r->replay == 5 && r->frame == 105 && r->first == 41 && r->last == 50);
    CHECK(rd_passring_replay_of(40) == NULL); /* replay 4: past the table */
    CHECK(rd_passring_replay_of(121) == NULL);
}

static void testFormat(void)
{
    rd__passring_reset();
    rd__passring_frame(318, 122, 1);
    rd__passring_list(11);
    rd__passring_push(RD_PASSK_PASS, 4, 1922, 1080, 0, 0, 0, 1, 3);
    rd__passring_pipeline(12, 2, 30, 1);
    rd__passring_push(RD_PASSK_COPY, 7, 688, 448, 8, 688, 448, 0, -1);
    rd__passring_push(RD_PASSK_PRESENT, 2, 1922, 1080, 0, 1920, 1080, 0, -1);
    char line[320];
    rd_passring_format(rd_passring_get(0), postName, line, sizeof(line));
    CHECK(strcmp(line, "#1 replay 318 frame 122 (keep) list 11: pass (storage), colour 4 "
                       "(1922x1080), effect fog, pipeline 12 (vs 2 fs 30 dst 1)") == 0);
    if (strstr(line, "#1 replay") == NULL) {
        fprintf(stderr, "got: %s\n", line);
    }
    rd_passring_format(rd_passring_get(1), NULL, line, sizeof(line));
    CHECK(strcmp(line, "#2 replay 318 frame 122 (keep) list 11: copy, target 7 (688x448) -> "
                       "target 8 (688x448)") == 0);
    rd_passring_format(rd_passring_get(2), NULL, line, sizeof(line));
    CHECK(strcmp(line, "#3 replay 318 frame 122 (keep) list 11: present, from target 2 "
                       "(1922x1080) to 1920x1080") == 0);
    /* a short buffer is cut, as snprintf cuts */
    char small[8];
    CHECK(rd_passring_format(rd_passring_get(0), NULL, small, sizeof(small)) > 7);
    CHECK(strlen(small) == 7);
}

int main(void)
{
    testOrder();
    testWrap();
    testFormat();
    printf("rd_passring_test: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}

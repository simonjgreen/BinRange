#include <assert.h>
#include <errno.h>
#include <string.h>
#include "tip_monitor.h"
static int32_t sample[3] = {0,0,1000};
static int read_error, save_error, reads, saves;
static br_tip_record saved;
static int read_sample(void *ctx, int32_t out[3]) {
    (void)ctx; ++reads; memcpy(out, sample, sizeof(sample)); return read_error;
}
static int save_record(void *ctx, const br_tip_record *r) {
    (void)ctx; ++saves; if (!save_error) saved = *r; return save_error;
}
static bool tick(br_tip_monitor *m, uint64_t t, bool moving) {
    return br_tip_monitor_update(m,t,moving,read_sample,save_record,0);
}
int main(void) {
    br_tip_monitor m;
    br_tip_monitor_init(&m, 0, 0);
    for (unsigned i=0;i<49;i++) assert(!tick(&m,i*40,false));
    save_error = -EIO;
    assert(!tick(&m,1960,false));
    assert(!br_tip_monitor_ready(&m) && m.error == -EIO && saves == 1);
    tick(&m,2000,false);
    assert(saves == 1); /* bound flash retry rate */
    save_error = 0;
    assert(tick(&m,2960,false));
    assert(br_tip_monitor_ready(&m) && saved.count == 0);
    int prior = reads;
    tick(&m,2961,false); assert(reads == prior);
    /* A wake just after an idle read still shortens the next deadline. */
    tick(&m,2961,true);
    assert(br_tip_monitor_deadline(&m) == 3000);
    /* A motion wake schedules a read now, not at the idle deadline. */
    tick(&m,3000,true); assert(reads == prior+1);
    read_error = -ENODATA; /* sensor clock may not align with the CPU's 40 ms timer */
    assert(!tick(&m,3040,true));
    assert(br_tip_monitor_ready(&m));
    prior = reads;
    tick(&m,3041,true); assert(reads == prior);
    assert(br_tip_monitor_deadline(&m) == 3045);
    read_error = 0;
    tick(&m,3045,true);
    sample[2] = -1000;
    assert(!tick(&m,3085,true)); assert(!tick(&m,3125,true));
    save_error = -EIO;
    assert(tick(&m,3165,true)); /* readiness becomes false */
    assert(!br_tip_monitor_ready(&m) && saved.count == 0);
    save_error = 0;
    assert(tick(&m,4165,true));
    assert(saved.count == 1 && br_tip_monitor_ready(&m));
    assert(br_tip_age_s(&m.policy,5165) == 2);
    read_error = -EIO;
    assert(tick(&m,4205,true)); assert(!br_tip_monitor_ready(&m));
    prior = reads;
    tick(&m,4206,true);
    assert(reads == prior); /* read failures must not spin on old sample time */
    read_error = 0;
    assert(tick(&m,5205,true)); assert(br_tip_monitor_ready(&m));
    read_error = -ENODATA;
    assert(!tick(&m,5245,true));
    for (unsigned t=5250;t<5365;t+=5) assert(!tick(&m,t,true));
    assert(tick(&m,5365,true));
    assert(!br_tip_monitor_ready(&m));
    assert(br_tip_monitor_deadline(&m) == 6365);
    prior = reads;
    tick(&m,5366,true); assert(reads == prior);
    read_error = 0;
    br_tip_monitor_init(&m,&saved,0);
    for (unsigned t=0;t<2000;t+=40) tick(&m,t,true);
    assert(m.policy.record.count == 1); /* reboot inverted doesn't double count */
    assert(br_tip_age_s(&m.policy,2000) == UINT32_MAX);
    br_tip_monitor_init(&m,0,-EINVAL);
    for (unsigned t=0;t<3000;t+=40) tick(&m,t,false);
    assert(!br_tip_monitor_ready(&m)); /* corrupt storage never rebaselines */
    return 0;
}

#include "update_policy.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Five progress samples, each 6000 ms apart; safe even near UINT64_MAX. */
static void thirty_seconds_progress(struct br_update_policy *p, uint64_t start)
{
    for (unsigned int i = 1; i <= 5; ++i) {
        br_update_policy_progress(p, start + i * UINT64_C(6000));
    }
}

static void confirmation_boundaries(void)
{
    struct br_update_policy p;
    br_update_policy_init(&p, 0, true);
    br_update_policy_health(&p, 0, true, true);
    for (uint64_t t = 6000; t <= 24000; t += 6000) {
        br_update_policy_progress(&p, t);
        br_update_policy_health(&p, t, true, true);
    }
    br_update_policy_progress(&p, 29999);
    assert(!br_update_policy_confirmation_ready(&p, 29999));
    br_update_policy_progress(&p, 30000);
    assert(br_update_policy_confirmation_ready(&p, 30000));
    assert(br_update_policy_confirmation_ready(&p, 36000));
    assert(!br_update_policy_confirmation_ready(&p, 36001));
    br_update_policy_health(&p, 36001, false, true);
    assert(!br_update_policy_confirmation_ready(&p, 36001));
    br_update_policy_health(&p, 36001, true, false);
    assert(!br_update_policy_confirmation_ready(&p, 36001));
    br_update_policy_init(&p, 0, false);
    br_update_policy_health(&p, 0, true, true);
    thirty_seconds_progress(&p, 0);
    assert(!br_update_policy_confirmation_ready(&p, 30000));
    br_update_policy_init(&p, 0, true);
    br_update_policy_progress(&p, 30000);
    assert(!br_update_policy_confirmation_ready(&p, 30000));
}

static void delayed_startup(void)
{
    struct br_update_policy p;
    br_update_policy_init(&p, 0, true);
    thirty_seconds_progress(&p, 0);
    br_update_policy_health(&p, 30000, true, true);
    assert(!br_update_policy_confirmation_ready(&p, 30000));
    for (uint64_t t = 36000; t <= 54000; t += 6000) {
        br_update_policy_progress(&p, t);
    }
    br_update_policy_progress(&p, 59999);
    assert(!br_update_policy_confirmation_ready(&p, 59999));
    br_update_policy_progress(&p, 60000);
    assert(br_update_policy_confirmation_ready(&p, 60000));
}

static void health_recovery(void)
{
    for (unsigned int subsystem = 0; subsystem < 2; ++subsystem) {
        struct br_update_policy p;
        br_update_policy_init(&p, 0, true);
        br_update_policy_health(&p, 0, true, true);
        thirty_seconds_progress(&p, 0);
        assert(br_update_policy_confirmation_ready(&p, 30000));
        br_update_policy_health(&p, 31000, subsystem != 0, subsystem != 1);
        assert(!br_update_policy_confirmation_ready(&p, 31000));
        br_update_policy_health(&p, 32000, true, true);
        assert(!br_update_policy_confirmation_ready(&p, 32000));
        for (uint64_t t = 36000; t <= 60000; t += 6000) {
            br_update_policy_progress(&p, t);
            br_update_policy_health(&p, t, true, true);
        }
        br_update_policy_progress(&p, 61999);
        assert(!br_update_policy_confirmation_ready(&p, 61999));
        br_update_policy_progress(&p, 62000);
        assert(br_update_policy_confirmation_ready(&p, 62000));
    }
}

static void progress_recovery(void)
{
    for (unsigned int health_first = 0; health_first < 2; ++health_first) {
        struct br_update_policy p;
        br_update_policy_init(&p, 0, true);
        br_update_policy_health(&p, 0, true, true);
        thirty_seconds_progress(&p, 0);
        assert(br_update_policy_confirmation_ready(&p, 36000));
        assert(!br_update_policy_confirmation_ready(&p, 36001));
        if (health_first) {
            br_update_policy_health(&p, 36001, true, true);
        }
        /* No health notification is necessary to detect a progress gap. */
        br_update_policy_progress(&p, 36001);
        assert(!br_update_policy_confirmation_ready(&p, 36001));
        for (uint64_t t = 42001; t <= 60001; t += 6000) {
            br_update_policy_progress(&p, t);
        }
        br_update_policy_progress(&p, 66000);
        assert(!br_update_policy_confirmation_ready(&p, 66000));
        br_update_policy_progress(&p, 66001);
        assert(br_update_policy_confirmation_ready(&p, 66001));
    }
}

static void maintenance_boundaries(void)
{
    struct br_update_policy p;
    br_update_policy_init(&p, 0, true);
    assert(!br_update_policy_access_allowed(&p, 0, true));
    assert(!br_update_policy_activity(&p, 0, true));
    br_update_policy_open_maintenance(&p, 1000);
    assert(!br_update_policy_access_allowed(&p, 1000, false));
    assert(br_update_policy_access_allowed(&p, 1000, true));
    assert(!br_update_policy_activity(&p, 60000, false));
    assert(br_update_policy_access_allowed(&p, 60999, true));
    assert(!br_update_policy_access_allowed(&p, 61000, true));
    assert(!br_update_policy_activity(&p, 61000, true));
    assert(!br_update_policy_access_allowed(&p, 61001, true));

    br_update_policy_open_maintenance(&p, 100000);
    assert(br_update_policy_activity(&p, 159999, true));
    assert(br_update_policy_access_allowed(&p, 219998, true));
    assert(!br_update_policy_access_allowed(&p, 219999, true));

    br_update_policy_open_maintenance(&p, 1000000);
    for (uint64_t t = 1050000; t <= 1550000; t += 50000) {
        assert(br_update_policy_activity(&p, t, true));
    }
    assert(br_update_policy_activity(&p, 1599999, true));
    assert(!br_update_policy_access_allowed(&p, 1600000, true));
    assert(!br_update_policy_activity(&p, 1600000, true));
    assert(!br_update_policy_access_allowed(&p, 1600001, true));
    /* Only a new trusted local entry may open a fresh window. */
    br_update_policy_open_maintenance(&p, 1700000);
    assert(br_update_policy_access_allowed(&p, 1700000, true));
    br_update_policy_init(&p, 1700001, false);
    assert(!br_update_policy_access_allowed(&p, 1700001, true));
}

static void wide_clock_boundaries(void)
{
    struct br_update_policy p;
    /* Boot 10000 ms before the 32-bit rollover; literal expected deadlines. */
    br_update_policy_init(&p, UINT64_C(4294957296), true);
    br_update_policy_health(&p, UINT64_C(4294957296), true, true);
    for (unsigned int i = 1; i <= 4; ++i) {
        br_update_policy_progress(&p, UINT64_C(4294957296) + i * UINT64_C(6000));
    }
    br_update_policy_progress(&p, UINT64_C(4294987295));
    assert(!br_update_policy_confirmation_ready(&p, UINT64_C(4294987295)));
    br_update_policy_progress(&p, UINT64_C(4294987296));
    assert(br_update_policy_confirmation_ready(&p, UINT64_C(4294987296)));
    assert(br_update_policy_confirmation_ready(&p, UINT64_C(4294993296)));
    assert(!br_update_policy_confirmation_ready(&p, UINT64_C(4294993297)));
    /* A full 32-bit epoch must not alias recent progress. */
    assert(!br_update_policy_confirmation_ready(&p, UINT64_C(8589954592)));
    assert(!br_update_policy_confirmation_ready(&p, UINT64_C(4294987295)));

    br_update_policy_open_maintenance(&p, UINT64_C(4294957296));
    assert(br_update_policy_access_allowed(&p, UINT64_C(4295017295), true));
    assert(!br_update_policy_access_allowed(&p, UINT64_C(4295017296), true));
    assert(!br_update_policy_access_allowed(&p, UINT64_C(8589924592), true));
    assert(!br_update_policy_access_allowed(&p, UINT64_C(4294957295), true));

    /* Deadline addition would overflow here; elapsed-time comparisons must not. */
    br_update_policy_init(&p, UINT64_MAX - 30000, true);
    br_update_policy_health(&p, UINT64_MAX - 30000, true, true);
    thirty_seconds_progress(&p, UINT64_MAX - 30000);
    assert(br_update_policy_confirmation_ready(&p, UINT64_MAX));
    br_update_policy_open_maintenance(&p, UINT64_MAX - 59999);
    assert(br_update_policy_access_allowed(&p, UINT64_MAX, true));
    br_update_policy_open_maintenance(&p, UINT64_MAX - 60000);
    assert(!br_update_policy_access_allowed(&p, UINT64_MAX, true));
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    if (strcmp(argv[1], "confirmation") == 0) confirmation_boundaries();
    else if (strcmp(argv[1], "maintenance") == 0) maintenance_boundaries();
    else if (strcmp(argv[1], "wide-clock") == 0) wide_clock_boundaries();
    else if (strcmp(argv[1], "delayed-startup") == 0) delayed_startup();
    else if (strcmp(argv[1], "health-recovery") == 0) health_recovery();
    else if (strcmp(argv[1], "progress-recovery") == 0) progress_recovery();
    else return 1;
    printf("update policy: %s passed\n", argv[1]);
    return 0;
}

#include "tip_monitor.h"
#include <string.h>
#include <errno.h>

void br_tip_monitor_init(br_tip_monitor *m, const br_tip_record *record, int error) {
    memset(m,0,sizeof(*m));
    br_tip_init(&m->policy,record);
    m->load_error = m->error = error;
    m->committed_count = m->policy.record.count;
}
bool br_tip_monitor_ready(const br_tip_monitor *m) {
    return m->policy.calibrated && m->healthy && !m->dirty && !m->error;
}
uint64_t br_tip_monitor_deadline(const br_tip_monitor *m) {
    return m->dirty && m->next_save < m->next_sample ? m->next_save : m->next_sample;
}
bool br_tip_monitor_update(br_tip_monitor *m, uint64_t now, bool moving,
                            br_tip_read_fn read, br_tip_save_fn save, void *context) {
    bool ready = br_tip_monitor_ready(m);
    uint32_t count = m->committed_count;
    if (m->load_error) { m->next_sample = now + 1000; return false; }
    /* Motion may arrive just after an idle sample. Even if another read is
     * too early, shorten the wait so the main owner cannot sleep for a second. */
    if (moving && m->healthy && m->policy.sampled && m->next_sample > now + 40)
        m->next_sample = m->policy.last_sample + 40;
    if (now >= m->next_sample) {
        int32_t sample[3];
        int rc = read(context,sample);
        /* Sensor and CPU clocks are asynchronous. No fresh sample is not a
         * failed bus: retry shortly without duplicating the previous sample.
         * Bound this grace period so a stalled sensor cannot stay ready. */
        if (rc == -ENODATA) {
            if (!m->no_data_deadline) m->no_data_deadline = now + 120;
            if (now < m->no_data_deadline) {
                m->next_sample = now + 5;
                return false;
            }
        } else m->no_data_deadline = 0;
        m->healthy = rc == 0;
        if (rc) { br_tip_invalid(&m->policy); m->error = rc; }
        else {
            if (!m->dirty) m->error = 0;
            if (br_tip_sample(&m->policy,sample[0],sample[1],sample[2],now,!moving)) {
                m->dirty = true;
                m->next_save = now;
            }
        }
        m->next_sample = now + (rc ? 1000 : br_tip_interval(&m->policy,moving));
    }
    if (m->dirty && now >= m->next_save) {
        int rc = save(context,&m->policy.record);
        m->error = rc;
        if (!rc) { m->dirty = false; m->committed_count = m->policy.record.count; }
        else m->next_save = now + 1000;
    }
    return ready != br_tip_monitor_ready(m) || count != m->committed_count;
}

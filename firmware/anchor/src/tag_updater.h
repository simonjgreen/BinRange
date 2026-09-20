#pragma once

#include "core/update_controller.h"

// Main-loop only. Snapshot reference remains valid until the next snapshot call.
void tag_updater_begin();
void tag_updater_loop();
const UpdateControllerSnapshot &tag_updater_snapshot();
const UpdateRelease *tag_updater_release();
uint32_t tag_updater_restart_count();
bool tag_updater_busy();
UpdateQueueResult tag_updater_pair(const UpdateAssociation &association, uint32_t pin);
UpdateQueueResult tag_updater_queue(uint16_t tag, uint32_t *id);
bool tag_updater_cancel(uint32_t id);
bool tag_updater_retry(uint32_t id);
UpdateQueueResult tag_updater_motion(uint16_t tag, uint32_t idle_ms, uint32_t moving_ms);
void tag_updater_motion_forget(uint16_t tag);
UpdateQueueResult tag_updater_stage_begin(const UpdateRelease &release);
bool tag_updater_stage_write(const uint8_t *bytes, size_t size);
bool tag_updater_stage_finish();
void tag_updater_stage_abort();

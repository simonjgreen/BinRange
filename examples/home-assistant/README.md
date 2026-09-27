# Collection and return reminders

[collection-reminders.yaml](collection-reminders.yaml) is a reusable copy of the
BinRange Home Assistant automation, with deployment entity IDs and bin names
replaced by placeholders. It uses the brilliant
[Waste Collection Schedule](https://github.com/mampfes/hacs_waste_collection_schedule)
integration for collection dates. The descriptive action names are also shown in
Home Assistant's visual automation editor.

## Schedule

All times use Home Assistant's configured local timezone, including its DST rules.
These are example defaults; change the time triggers to suit your household.

| When | Action |
| --- | --- |
| 18:00 the evening before collection | Remind about due bins still confidently Home |
| 21:00 the evening before collection | Urgent reminder about due bins still Home |
| 06:00 on collection day | Urgent reminder about due bins still Home |
| 18:00 the day after collection | Bring-in reminder about that collection's bins still confidently Out |

For example, a Tuesday collection has its bring-in reminder on Wednesday evening.
Urgency changes the title and wording; it does not bypass silent or Do Not Disturb
settings. Each phase produces one grouped message to the configured recipients.

## Configure before enabling

Replace every `replace_me_*` placeholder, including occurrences inside templates:

| Placeholder | Replace with |
| --- | --- |
| `calendar.replace_me_collection_schedule` | Your Waste Collection Schedule calendar entity |
| `notify.replace_me_phone_group` | Your notify **entity group**, containing the intended phone notify entities |
| `input_text.replace_me_collection_dates` | A Text helper for saved collection categories |
| `input_text.replace_me_reminder_phases` | A Text helper for attempted reminder phases |
| `replace_me_bin_1` through `replace_me_bin_6` | The bin slugs used by your location and freshness entities |

1. Create a notify group under **Settings → Devices & services → Helpers → Create
   helper → Group → Notify group**. Select the intended phone notify entities.
   This example uses `notify.send_message` with the group as its target; it does
   not use `notify.notify` or a YAML notify-action group.
2. Create both Text helpers with a maximum length of **255**. Set each helper's
   current value to `{}` once. Leave any configured `initial` value unset so the
   history restores across restarts; do not reset the helpers on startup.
3. Edit the `bins` list in **Prepare bin names, local dates and reminder phases**.
   Replace display names, slugs and categories, and add or remove entries as
   needed. The generic example maps two bins to `recycling`; one calendar
   category can legitimately cover several physical bins.
4. The two category-extraction templates accept `waste`, `recycling`, `food`,
   `garden` and `glass`. Replace these lists with your provider's category names
   if different. Categories are trimmed and lowercased, and comma-separated
   event summaries are supported. Use those same lowercase values in `bins`.
5. Each slug builds `sensor.<slug>_location` and
   `binary_sensor.<slug>_range_freshness_a`. Adapt those templates if your entity
   naming differs. Location must be `Home`, `Out` or unknown; the freshness
   entity must be **off when fresh**. Unknown, unavailable and stale positions
   are excluded. Establish a useful location threshold for your installation.
6. Paste the YAML as a single automation in HA's automation YAML editor. If you
   maintain an `automations.yaml` list instead, add it as one list item. Give it
   a unique automation ID if another copy already exists. Deliberately disable
   overlapping reminders when enabling this one.

## Behavior and limits

The calendar is queried for each put-out phase, so changed dates are respected.
Because providers may remove past events, the automation also saves today's and
yesterday's collection categories. It refreshes that record at 00:05, at reminder
times and on HA startup. The history-only runs do not notify. The first bring-in
reminder needs a collection-day snapshot; installation cannot reconstruct an
expired calendar event. Keep the stored category lists within the helper's
255-character limit.

An explicit If/Then block sends only when bins need attention and that phase has
not already been attempted for the collection date. The phase is recorded before
sending to avoid duplicate attempts after partial delivery or reruns. Delivery
errors remain in HA's trace and are not retried automatically. A skipped phase
does not block another phase due at the same time.

The initial If/Then Stop guards reject manual runs and unavailable or unreadable
history. Consequently, **Run actions is intentionally not a phone-delivery test**.
The scheduled times must occur while HA is running; missed times are not replayed
after startup. Validate your own entity mappings and inspect traces during the
first real collection cycle.

The bring-in reminder means the bins are still out after their scheduled date.
It does not establish that they were emptied. The optional tipping package below
adds a separate physical emptying assumption; the scheduled reminder itself does
not use a collection-detection classifier or settling filter.

The source automation passed notification-free execution checks in Home Assistant
2026.9.2 for grouping, duplicate suppression, changed dates, stale/Unknown data,
return history, independent evening phases, history guards and a DST transition.
The published placeholders must be configured before use.

## Tipping-triggered emptying messages

[tipping-notifications.yaml](tipping-notifications.yaml) adds the emptying message
shown in the main README. It requires tipping-capable tag **and** anchor firmware.
The tag detects a tilt strictly greater than 90 degrees from its upright reference;
that is treated as evidence the bin has been emptied, not proof of a collection.
Exactly 90 degrees, ordinary movement and location changes do not trigger it.
The original scheduled automation remains enabled as the fallback, including its
next-evening bring-in reminder. This package does not change its phase history.

Install this file as a Home Assistant **package**, or create its two Text helpers
and paste the `automation` list's single object into the automation YAML editor.
For packages, enable your package directory under `homeassistant: packages:` using
Home Assistant's [package configuration](https://www.home-assistant.io/docs/configuration/packages/).
Configure it while disabled, then:

1. Replace the calendar, notify group and saved collection-date placeholders with
   the same entities as the scheduled reminder. Replace every bin slug, name and
   category. Set each tag to its four-digit lowercase UWB tag ID and replace the
   MQTT anchor ID. MQTT uses the raw `binrange/tag/<tag>/anchor/<anchor>/state`
   topic; tipping discovery entity names are not dependencies.
2. Keep numeric bin IDs `1` through `6` stable. The two new Text helpers are
   `input_text.binrange_tip_ledger` and `input_text.binrange_tip_batch`, each with
   maximum length 255. Set their values to `{}` **once when first installing**.
   Leave `initial` unset so HA restores their history. Do not reset them on startup,
   reload, firmware update or reconnect. Missing or malformed history stops the
   automation without overwriting it.
3. Match the fresh-location and motion-sensor-fault entity templates to your
   installation. As in the reminder, freshness is `off` when fresh. Sensor fault
   must be `off`; unavailable or unknown health is excluded. The example uses
   anchor suffix `_a` in these entity IDs; update all occurrences for other anchors.
4. Validate the configuration and enable one copy. Manual **Run actions** remains
   deliberately inert. Inspect traces on the next genuine collection instead of
   sending demonstration phone notifications.

A healthy, nonstale report must contain a positive persistent counter, a valid
reception timestamp and an event age. The event time is reception time minus age;
future timestamps and events older than 30 minutes are rejected. A recent genuine
event may qualify on its first HA connection, including recovery after a brief
radio gap. A repeated or older counter never creates a new event. Old firmware's
null tipping fields and a reboot's unknown event age do not create events.

HA saves the candidate before checking its location, then rechecks every five
seconds for up to 30 minutes. This avoids losing a tip when the first range report
briefly says Home/unknown or the location entity has not updated yet. It requires
**fresh Out** and healthy motion telemetry before queueing and again before
notification. A bin brought Home before delivery is excluded. The calendar must
schedule that bin's category on the event's local date. For an event just before
midnight, yesterday's saved reminder history is used if the provider has already
removed those events; today's calendar always takes precedence over saved history.

The first qualifying bin opens a **fixed 60-second window**. Other bins can join
without extending it. The five-second clock flushes after the deadline (normally
within five seconds), or after a restart once dependencies have recovered. Bins whose location or health is still unknown, unavailable or stale stay pending
at their original deadlines for up to the 30-minute recovery limit; other resolved
bins can notify immediately. Once their dependencies recover, deferred bins can
notify without another 60-second wait. Fresh Home suppresses that bin. New tips
after an expired window get a new fixed window even while an older bin is deferred.
An event older than the 30-minute recovery limit is discarded. The queued automation has
no blocking delay, so one bin cannot prevent another from joining. A new bin
arriving after the deadline opens a new window. Messages use singular/plural forms
and a natural list, for example:

> **The bins have just been emptied**
> General Waste, Recycling 1 and Glass have just been emptied. You can now bring them in.

The batch helper maps compact bin IDs to their individual deadlines, allowing
expired unresolved bins and a newer grouping window to coexist. The compact
ledger stores each bin's highest accepted counter, event timestamp
and last attempted collection date. Each bin is attempted at most once per local
collection date, even if tipped more than once. Attempts and batch removal are
saved before notification delivery; failed delivery is recorded in the HA trace
and is not retried automatically. This also means an interruption between marking
an attempt and sending can lose a notification; the scheduled reminder remains
the fallback. HA helper restoration protects normal restarts, not transactional
exactly-once delivery across abrupt power loss.

The six-slot limit keeps even maximum counters below HA's 255-character helper
limit (compact numeric keys avoid storing long entity IDs). For more than six bins,
split into separate instances with unique helpers/automation IDs, or redesign the
storage. Replacing or factory-resetting a tag with a lower counter requires a
controlled reset of **that bin's** ledger entry and pending membership while the
automation is disabled; never clear the entire ledger to fix a single tag.

Run `python3 scripts/test_ha_tipping.py` for notification-free execution of the
actual shipped templates and action branches. The local runner is deliberately
small; native HA configuration validation and fixture execution are also required
before deployment. Neither test method needs a phone push.

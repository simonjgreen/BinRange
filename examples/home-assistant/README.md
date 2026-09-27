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
It does not establish that they were emptied. No collection-detection classifier,
settling filter or health-warning automation is included.

The source automation passed notification-free execution checks in Home Assistant
2026.9.2 for grouping, duplicate suppression, changed dates, stale/Unknown data,
return history, independent evening phases, history guards and a DST transition.
The published placeholders must be configured before use.

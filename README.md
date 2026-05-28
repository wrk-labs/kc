# kc

A minimal terminal calendar for [kai](https://github.com/wrk-labs/kai) Linux. Built with C and ncurses.

kc manages calendars locally using the vdir format, with support for CalDAV sync and ICS subscriptions.

## Features

- **Month view** — navigate days, weeks, months with keyboard
- **Event management** — create, edit, delete events with recurrence support
- **CalDAV sync** — two-way sync with iCloud and other CalDAV servers
- **ICS subscriptions** — read-only subscriptions to remote .ics feeds
- **Multiple calendars** — color-coded, individually toggleable
- **RSVP** — view attendees and respond to invitations
- **Inline UI** — all forms and menus render within the main interface

## Dependencies

- libical
- libcurl
- ncurses

## Install

```
make && sudo make install
```

## Usage

```
kc              # launch calendar
kc -s           # sync all calendars
kc -v           # print version
```

## Configuration

Config lives in `~/.kc/config`. Calendars can be managed interactively by pressing `c` inside kc.

## License

GPL-2.0 — see [LICENSE](LICENSE).

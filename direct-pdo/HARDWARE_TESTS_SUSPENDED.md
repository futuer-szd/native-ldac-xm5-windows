# Direct-PDO hardware tests are suspended

The 2026-07-21 hardware incident left the XM5 Bluetooth/PnP stack and Direct render endpoint present after the headset was reported powered off. Direct-PDO installation and hardware trials must remain blocked while this file exists.

Removing this file requires a reviewed source commit that documents all of the following offline evidence:

- engine readiness is established before AVDTP START;
- physical presence is independent from WaveRT RUN and media fault state;
- remote disconnect has priority over media recovery and endpoint retention;
- repeated/discarded callbacks, surprise removal, long-lived RUN and missing engine are covered by host contract tests;
- the rollback path has a verified original A2DP target and a separate recovery environment.

There is intentionally no command-line override.

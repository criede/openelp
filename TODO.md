Todo Items
==========

Features
--------
* More args for Windows service
* Publish to statsd
* Provide fail2ban filter
* Support pause/continue in Windows service
* Support socket activation in systemd
* Support CIDR notation for AdditionalExternalBindAddresses

Optimizations
-------------
* Re-use same slot on reconnect
* Skip unnecessary registration updates
* Stop allocating new memory after proxy\_start

Additional Settings
-------------------
* Timeout (ConnectionTimeout in config)
* Inactivity timeout
* TCP connection whitelisting
* TCP Retry Count
* Max data packet size
* Buffer sizes
* Configurable SO\_KEEPALIVE
* Configurable SO\_REUSEADDR
* Allow/reject duplicate callsigns

Packages
--------
* Create the APT_SIGNING_KEY secret and verify the first runs of the
  daily build and the release backlog (see .github/UPSTREAM-SYNC.MD)
* Have native speakers review the AI-translated APT repository start pages
* Optional libopenelp-dev package (headers, unversioned .so)

Security
--------
* Complete security check
* Implement all possible mitigations for malformed payloads that might trigger bugs in svxlink code
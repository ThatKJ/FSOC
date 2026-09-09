# Security Policy

FSOC is a research/hackathon simulation and evaluation testbed. There is no
production deployment handling user accounts, payments, or sensitive personal
data — the public web deployment is a read-only project site and deterministic
replay viewer (see `docs/DEPLOYMENT.md`), and the phone-camera prototype
(`fsoc_live`) processes camera frames locally on the machine that runs it and
does not transmit them anywhere.

## Reporting a vulnerability

If you find a security issue (e.g. a way to make a public API route read
local files it shouldn't, or execute anything server-side), please open a
GitHub issue on `ThatKJ/FSOC` describing it. Since this project has no users
or data at risk beyond its own source and demo evidence, there is no formal
disclosure SLA — but real reports are read and fixed.

Please do not open an issue containing a working exploit against a third
party; describe the class of problem instead.

# Security

AC Customs is local desktop software that loads into and inspects a legacy game client. Treat native-hook changes with the same care as any in-process systems code.

Do not publish reports containing credentials, private paths that reveal sensitive information, or unrelated process memory. If a security issue could expose arbitrary local files, execute unintended code, or corrupt memory outside the intended AC client process, report it privately to the project maintainer before opening a public issue.

The project does not require API keys or online service credentials. Any such secrets found in a local development environment should never be committed.

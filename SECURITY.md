# Security Policy (Vulnerability Disclosure Policy)

Cryptographic security and memory integrity are core priorities of the **Kheiron** project.

## Supported Versions

Only the current production release line receives active security updates and fixes:

| Version | Supported |
| :---: | :---: |
| `v2.0.x` | Yes |
| `< v2.0.0` | No |

## Reporting a Vulnerability

**Do not open public GitHub issues** to report security flaws, state leaks, entropy failures, side-channels, or memory vulnerabilities.

Please use one of the private channels below:

- **GitHub Security Advisories** (preferred): open a *Private Vulnerability Report* directly from the **Security** tab of `cainamatera/kheiron`.
- **Direct Contact**: send reproducible technical details (version, compiler, flags, operating system, and PoC) to the maintainer (`matera.caina@gmail.com`).

## Triage and Response Timelines

- **Initial triage**: within **48 hours** of receiving the report, with acknowledgment and preliminary assessment.
- **Critical vulnerability resolution**: within **7 days** for critical or high-severity flaws (e.g., entropy degradation, forward secrecy bypass, buffer overflow, or post-`fork` failures), with coordinated responsible disclosure prior to publishing the advisory.

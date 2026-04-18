# Security Policy

## Reporting a Vulnerability

If you discover a security vulnerability in Kartend, please report it responsibly.

**Email:** etheraura@protonmail.com

Please include:
- Description of the vulnerability
- Steps to reproduce
- Potential impact
- Suggested fix (if any)

You will receive an acknowledgment within 48 hours. Security issues will be prioritized and a fix released as soon as practical.

## Security Considerations

Kartend launches user-configured external processes. The launch module includes:
- Executable path validation and permission checks
- Sensitive directory blacklisting (system paths, `/root`, etc.)
- TOCTOU mitigation with re-validation before execution
- Argument list passing (no shell interpolation)

SQL queries use parameterized binding throughout; FTS input is sanitized.

## Supported Versions

| Version | Supported |
|---------|-----------|
| 0.0.x   | ✅        |

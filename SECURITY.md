# Security policy

AYTHER Runtime is a prerelease product, but dependency and secret findings are
treated as release-blocking engineering defects.

## Reporting

Do not open a public issue for a suspected vulnerability or exposed secret.
Use GitHub's private vulnerability reporting for this repository. Rotate or
revoke exposed credentials before investigating their history.

## Pull-request gates

- GitHub CodeQL default setup analyzes Actions and C++.
- GitHub secret scanning and push protection detect supported provider tokens.
- `Security / Secret Scan` runs TruffleHog OSS over the pull-request range.
- `Security / Dependency Review` rejects newly introduced dependencies with a
  high or critical advisory. Medium and low findings remain visible for triage.
- All workflow actions are referenced by immutable commit SHA.

## Remediation and exceptions

Critical findings target remediation within 24 hours; high findings within
seven days; medium findings within 30 days. A high/critical dependency may be
excepted only when no fixed compatible release exists and a private security
record documents the owner, affected versions, compensating control, expiry
date (at most 30 days), and rollback. Renewals require a new review.

Secret findings are never excepted. False positives must be suppressed at the
narrowest possible path or rule and include an inline rationale that contains
no secret material. Disabling a repository-wide scanner is not an exception.

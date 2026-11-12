# Defender false-positive playbook

## Scope

Use this when any release binary is flagged by Microsoft Defender or SmartScreen.

## Prerequisites

- Release built from a clean tagged commit.
- Code-signing secrets configured in repository settings:
  - `WINDOWS_SIGNING_CERT` (base64-encoded `.pfx`)
  - `WINDOWS_SIGNING_PASSWORD`
- Published release artifacts include `SHA256SUMS.txt`.

## Fast triage

1. Download flagged file from the release page.
2. Verify SHA-256 matches the release `SHA256SUMS.txt`.
3. Verify digital signature is present and valid.
4. Confirm detection name and engine version from Defender history.

## Microsoft submission

1. Submit file to Microsoft Security Intelligence portal:
   - https://www.microsoft.com/en-us/wdsi/filesubmission
2. Choose software developer submission type.
3. Include these details:
   - Product: Legacy Notepad
   - Version/tag
   - Architecture (x86/x64/ARM64)
   - SHA-256
   - Direct release URL
   - Detection name shown by Defender
4. Request malware classification review and false-positive removal.

## Follow-up checklist

- Track Microsoft submission ID in issue #26.
- Re-scan after Microsoft response.
- If cleared, post confirmation and close issue.
- If still flagged, submit again with latest signed build and updated hashes.

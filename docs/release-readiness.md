# Desktop Release Readiness

The current macOS and Windows ZIPs are development test packages, not cleared
public releases. Passing CI does not establish license compliance or compatibility
with a clean operating system. Do not publish a release until the gates below
have evidence recorded for the exact candidate ZIP and its SHA-256 hash.

## Licensing and Provenance

- [ ] Owner selects a microspice license and adds its full text. There is currently
  no project license; public source visibility is not a redistribution license.
- [ ] Include the selected project license in both desktop packages.
- [x] Include a Qt-versioned provenance note, SDK SPDX documents, and full LGPLv3
  and GPLv3 companion texts. These are installed before macOS bundle signing.
- [ ] Inventory the actual deployed binaries/plugins against the SDK SBOMs.
  SDK documents may include build tools and modules not redistributed; copying
  all available SDK SBOMs is not a substitute for a binary-level audit.
- [ ] Collect every applicable third-party copyright notice and license text,
  including dependencies inside Qt libraries and plugins. Review platform-specific
  font, image, compression, cryptography and other dependency terms.
- [ ] Record exact Qt version, SDK origin and build options. Preserve corresponding
  source and any patches for Qt and applicable bundled dependencies. Verify the
  selected compliance mechanism for supplying that source to recipients; a generic
  archive link in a notice is not a completed source-availability obligation.
- [ ] Verify LGPL replacement/relinking and debugging rights for these dynamically
  linked builds, including how a user can re-sign a modified macOS bundle.
- [ ] Confirm Microsoft runtime redistribution eligibility under the Visual Studio
  license used by the Windows build. Keep Microsoft terms distinct from Qt terms.

This is an engineering checklist, not legal advice or a compliance certification.

## Clean-Machine Acceptance

Use a fresh supported OS VM or separate machine with no Qt SDK, compiler,
Visual Studio runtime installation beyond the OS baseline, or development PATH.
Do not install build tools to run the acceptance test. Transfer the candidate ZIP
through the same download channel intended for users and record OS/architecture.

- [ ] Extract into a directory containing spaces; launch by double-clicking.
- [ ] Record Gatekeeper/SmartScreen prompts without disabling security policy.
- [ ] Open both included examples, run divider DC and RC transient analysis.
- [ ] Edit a component, exercise undo/redo, save a project and reopen it.
- [ ] Select a waveform probe, export CSV, inspect solver diagnostics.
- [ ] Start and cancel an analysis; confirm editing and closing remain responsive.
- [ ] Move the entire app/folder elsewhere and repeat launch and example loading.
- [ ] Check readable layouts at common display scaling settings and compact size.
- [ ] Record screenshots, failures and the candidate SHA-256; do not infer this
  result from hosted CI's SDK-path-stripped smoke test.

## Signing and Publication

- [ ] macOS: owner supplies Developer ID signing locally or through a protected
  CI environment; sign nested code, enable required hardened runtime settings,
  notarize and staple. Verify the downloaded artifact on another Mac.
- [ ] Windows: decide whether to publish unsigned with an explicit warning or
  Authenticode-sign and timestamp the executable. Review trust warnings on a
  separate Windows machine. ZIP packaging alone does not establish publisher trust.
- [ ] Never put certificates, passwords, signing keys or notarization credentials
  in source control, chat, artifacts or ordinary build logs.
- [ ] Verify the final signed artifacts, compute SHA-256 hashes, record source
  commit, dependency versions, minimum OS/architecture and known limitations.
- [ ] Publish deliberate release artifacts with accompanying notices and source
  compliance materials. CI's 14-day artifacts are temporary test downloads.

## Current Automated Checks

Local and hosted tests cover core/GUI behavior. Package verification checks native
launch, dependency paths, required notices, and signature integrity on macOS;
Windows checks required DLLs, icon/version resources and SDK-path-independent
launch. CI also extracts ZIPs and tests relocated packages. These checks do not
cover release signing, complete licensing, or a pristine machine.
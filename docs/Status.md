# Status

Pre-release. [DS Spec 2.4](ds-spec-2.4.md) and the C++ API are being designed together. Breaking API changes are made without a deprecation period while that remains true.

## SynthUnit migration

The manifest and Package loading layer implements DataOnly inspection, internal Probe, dependency selection, contribution reference binding, interpreter discovery, Acquire and Ready validation, atomic Commit visibility, shared Package identities, and strong reference based release. The built in inference and singer categories use the new contribution interfaces.

## TODO

- Define the runtime execution and ImportBinding interfaces needed to implement provider prepare, commit, abort, quit, and wait without putting implementation specific scheduling into SynthUnit.
- Migrate dsinfer interpreters, singer providers, inference drivers, utilities, and the command line front end from NamedObject, UNO, ContribLocator, and PackageRef to the new typed contribution objects and PackageHandle.
- Implement the DSPK Installer separately from the directory only Package Loader.
- Publish user documentation after the C++ API and DS Spec 2.4 stabilize.

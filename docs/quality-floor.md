# Quality floor baseline (2026-09-18)

`compileJava` now runs with `-Xlint:all` (build.gradle) so every push shows
exactly what javac sees. First full sweep, 2,949 source files:

| category    | count | verdict |
|-------------|-------|---------|
| deprecation |    44 | decompiled code calling old JDK APIs; review case-by-case |
| rawtypes    |    30 | decompiled generics loss; cosmetic, high churn risk to "fix" |
| unchecked   |    22 | same root cause as rawtypes |
| cast        |     4 | redundant casts, harmless |

Total: 100 warnings, 0 errors, build green.

Policy: this repo is recovered decompiled code (obfuscated member names).
Mechanically cleaning these warnings touches injection-critical code that
has no runtime test harness, so the floor is VISIBILITY, not cosmetic
churn: new code must not add warnings, and the count above is the ceiling.
Fix warnings only in files you are already changing for a real reason.

Next steps: SpotBugs/Error Prone in CI once the workflow is enabled
(blocked on token workflow scope), and unit tests for the
Minecraft-independent logic (protocol state, config parsing).

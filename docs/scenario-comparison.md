# Comparing captured scenarios

`scripts/compare_scenarios.py` is a small post-processing tool for two BootHush JSON results with frame-series observations. Each side must have a successful supported stop, a written series, matching declared machine, ROM and executable SHA-256, reached readiness if reported, and no pending scheduled input. Each check selects a processed `memory`, `ppu`, or `oam` observation, decodes 1, 2, 4 or 8 bytes with explicit byte order and signedness, applies an explicit rational scale and bias, and compares the values and modeled elapsed times with explicit tolerances. Both sides declare their own clock in cycles per second and origin cycle; elapsed time is `(sample cycle - origin cycle) / clock`. Frames and VBLs are not assumed to correspond.

```powershell
python scripts/compare_scenarios.py --source source.json --target target.json `
  --mapping mapping.json --report new-report.json
```

For example, replace the hashes and origin cycles with values from the corresponding results, and select addresses actually captured by each run:

```json
{
  "source": {"machine":"nes", "rom_sha256":"<hash>", "executable_sha256":"<hash>", "clock_hz":1662607, "origin_cycle":1000},
  "target": {"machine":"mac128k", "rom_sha256":"<hash>", "executable_sha256":"<hash>", "clock_hz":7833600, "origin_cycle":2000},
  "checks": [{
    "name":"mapped position",
    "source":{"sample":0,"field":"memory","address":16,"width":1,"byte_order":"little","signed":false,"scale":[1,1],"bias":[0,1]},
    "target":{"sample":0,"field":"memory","address":2816,"width":2,"byte_order":"big","signed":true,"scale":[1,256],"bias":[0,1]},
    "value_tolerance":[1,16], "time_tolerance_us":[20000,1]
  }]
}
```

All fractions are `[numerator, positive_denominator]`; tolerances must be nonnegative. One to 64 named checks are allowed. Sample indices are zero-based. The report retains raw selected bytes, decoded and normalized values, exact rational differences, selectors, full mapping, and hashes and paths of the original result files. Exit code 0 means all declared checks match, 1 means at least one value or timing comparison fails, and 2 means missing/invalid evidence or output failure. A report path must not already exist. The input results and captures are not modified.

The helper does not orchestrate ROMs, map logical actions to inputs, verify that user-provided origins or transformations are semantically correct, validate capture image files, compare pixels or audio, or establish game/hardware equivalence. Keep the original scripts, ROM and executable identities, capture files and their manifests with the report; check witness truncation and errors separately. Distinguish an invalid-evidence result from a mismatch. ROM/source identity must be independently established before attaching source labels to observed state.

The test suite runs independent, generated NES and Mac ROMs, captures little- and big-endian representations of the same marker at their respective readiness boundaries, and tests equal values, a value mismatch, a time mismatch, missing samples, wrong identities and unchanged input files. This is a comparison-mechanism fixture, not a game movement scenario or a claim about the port.

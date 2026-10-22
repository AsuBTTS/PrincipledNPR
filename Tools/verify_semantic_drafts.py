"""Check draft IDs, finite values, hard domains and report provenance.

This is a bootstrap audit, not the future Resolver or a full JSON Schema validator.
It makes no claim about calibration, shader mapping or rendering acceptance.
"""
import hashlib
import json
import math
from pathlib import Path

root = Path(__file__).resolve().parents[1]
docs = root / 'Documentation'
contract = json.loads((docs / 'Schemas/NPRParameterContract.v1.json').read_text(encoding='utf-8'))
schema = json.loads((docs / 'Schemas/NPRStyleProfile.schema.v1.json').read_text(encoding='utf-8'))
definitions = {item['id']: item for item in contract['parameterDefinitions']}
assert len(definitions) == len(contract['parameterDefinitions']), 'Duplicate parameter ID'
report_hash = hashlib.sha256((docs / 'Principled_NPR_技术设计报告_V1.0.md').read_text(encoding='utf-8').encode('utf-8')).hexdigest()
assert contract['source']['sha256'] == report_hash, 'Source report changed; review draft provenance'

for path in sorted((docs / 'Styles').glob('*.v1.draft.json')):
    style = json.loads(path.read_text(encoding='utf-8'))
    assert style['sourceContract']['reportSha256'] == report_hash, path.name
    assert style['schemaVersion'] == contract['schemaVersion'] == 1
    assert style['status'] == 'Draft' and style['implementationState']['renderTests'] == 'NotRun'
    assert set(schema['required']).issubset(style), 'Missing required top-level field'
    assert set(style).issubset(schema['properties']), 'Unknown top-level field'
    groups = [style['defaultsPatch'], *style['materialPatches'].values()]
    for patch in groups:
        for identifier, entry in patch.items():
            assert identifier in definitions, 'Unknown parameter: ' + identifier
            assert set(entry) == {'isSet', 'value'} and isinstance(entry['isSet'], bool)
            value = entry['value']
            rgb = definitions[identifier].get('valueType') == 'linear_rgb'
            if rgb:
                assert isinstance(value, list) and len(value) == definitions[identifier]['componentCount'] == 3
            values = value if rgb else [value]
            domain = definitions[identifier]['hardDomain']
            low, high = domain if domain is not None else (None, None)
            for component in values:
                assert isinstance(component, (int, float)) and not isinstance(component, bool) and math.isfinite(component)
                assert low is None or component >= low, identifier + ' below hard domain'
                assert high is None or component <= high, identifier + ' above hard domain'
    print(path.name + ': draft data checks PASS')
print('IDs, domains and provenance PASS; full JSON Schema/rendering validation not performed.')

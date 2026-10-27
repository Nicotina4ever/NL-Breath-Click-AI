# SPDX-License-Identifier: MPL-2.0
"""Decode an exported blind vote against its study key, without training or editing audio."""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path


def summarize(study, votes):
    report = json.loads((study/'report.json').read_text(encoding='utf-8-sig'))
    key = json.loads((study/'CHIAVE_PRIVATA.json').read_text(encoding='utf-8-sig'))
    raw = votes.read_bytes()
    feedback = json.loads(raw.decode('utf-8-sig'))
    if (feedback.get('schema') != 1 or
            feedback.get('type') != 'nl.breathclick.listening-preferences' or
            feedback.get('training_eligible') is not False or
            not report.get('study_id') or
            feedback.get('study_id') != report['study_id'] or
            key.get('study_id') != report['study_id']):
        raise ValueError('Feedback schema, training exclusion or study identity mismatch')
    cases = {c['id']: c for c in report['cases']}
    if len(cases) != len(report['cases']) or set(key['mapping']) != set(cases):
        raise ValueError('Study/key cases mismatch')
    answers = feedback.get('answers')
    if not isinstance(answers, dict) or set(answers)-set(cases):
        raise ValueError('Unknown feedback cases')
    totals = Counter()
    groups = defaultdict(Counter)
    decoded = []
    for case_id, case in cases.items():
        mapping = key['mapping'][case_id]
        if set(mapping) != set('ABC') or set(mapping.values()) != {'auto','linear','predictive'}:
            raise ValueError('Invalid blind mapping')
        if any(case['variants'][letter]['method'] != method for letter, method in mapping.items()):
            raise ValueError('Report/key method mismatch')
        answer = answers.get(case_id, {})
        choice = answer.get('preferred', '')
        if choice not in ('','A','B','C','Originale / nessuna','Parita'):
            raise ValueError('Invalid preference')
        preferred = mapping.get(choice, {'':'unanswered','Originale / nessuna':'original','Parita':'tie'}.get(choice))
        totals[preferred] += 1
        groups[case['group']][preferred] += 1
        ratings = {}
        for letter in 'ABC':
            metrics = {}
            for metric in ('rimozione','voce','artefatti'):
                value = answer.get(letter+'_'+metric, '')
                if str(value) not in ('','1','2','3','4','5'):
                    raise ValueError('Ratings must be absent or integers from 1 to 5')
                if value != '':
                    metrics[metric] = int(value)
            if metrics:
                ratings[mapping[letter]] = metrics
        auto = next(v for v in case['variants'].values() if v['method']=='auto')
        decoded.append(dict(id=case_id, original_group=case['group'], preferred=preferred,
                            ratings=ratings, note=answer.get('note',''),
                            auto_applied=auto['applied'], auto_declined=auto['declined']))
    return dict(schema=1, study_id=report['study_id'],
                feedback_sha256=hashlib.sha256(raw).hexdigest(),
                tested_source_commit=report.get('verified_source_commit'),
                training_eligible=False,
                scope='One listener; clip preferences, not event labels or detection accuracy. '
                      'All voted recordings are development data for subsequent tuning.',
                preferences=dict(totals), original_groups={g:dict(c) for g,c in groups.items()},
                cases=decoded)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--study', type=Path, required=True)
    p.add_argument('--votes', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    result = summarize(a.study, a.votes)
    repo = Path(__file__).resolve().parents[1]
    output = a.out.resolve()
    if not output.is_relative_to(repo/'build'):
        raise SystemExit('Private feedback report must stay under build/')
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding='utf-8')
    print(json.dumps(dict(study_id=result['study_id'], preferences=result['preferences'],
                          training_eligible=False, report=str(output))))


if __name__ == '__main__':
    main()

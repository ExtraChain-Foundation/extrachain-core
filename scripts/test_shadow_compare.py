import copy
import unittest

from shadow_compare import summarize_comparison


def pairs(baseline, candidate):
    return {index: {build: {'complete': True, 'latency_ms': latency,
                            'intents_per_second': 288000 / latency, 'cpu_s': 1,
                            'peak_rss_bytes': 1, 'read_bytes': 1, 'write_bytes': 1,
                            'dfs_from_barrier_s': 1}
                    for build, latency in (('baseline', before), ('candidate', after))}
            for index, (before, after) in enumerate(zip(baseline, candidate), 1)}


class ComparisonTest(unittest.TestCase):
    def test_ratio_of_median_rates_matches_the_approved_criterion(self):
        result = summarize_comparison(pairs([9000, 5000, 9000, 9000, 5000],
                                            [4000, 4000, 7000, 7000, 4000]))
        self.assertAlmostEqual(result['median_rate_ratio'], 2.25)
        self.assertLess(result['median_throughput_ratio'], 1.5)
        self.assertFalse(result['all_pairs_at_least_1_5'])
        self.assertTrue(result['target_met'])

    def test_mean_gain_does_not_replace_median_gain(self):
        result = summarize_comparison(pairs([4000, 4000, 4000, 9000, 9000], [3000] * 5))
        self.assertFalse(result['target_met'])

    def test_three_resource_regressions_reject_the_candidate(self):
        data = pairs([9000] * 5, [4000] * 5)
        for index in (1, 2):
            data[index]['candidate']['cpu_s'] = 1.1
        self.assertTrue(summarize_comparison(data)['target_met'])
        data[3]['candidate']['cpu_s'] = 1.1
        self.assertFalse(summarize_comparison(data)['target_met'])

    def test_incomplete_stands_and_missing_pairs_do_not_pass(self):
        data = pairs([9000] * 5, [4000] * 5)
        incomplete = copy.deepcopy(data)
        incomplete[1]['candidate']['complete'] = False
        for invalid in (incomplete, {key: value for key, value in data.items() if key != 5}):
            with self.assertRaises(ValueError):
                summarize_comparison(invalid)


if __name__ == '__main__':
    unittest.main()

//! SequenceMatcher's matching-block algorithm with no junk predicate. Keep its
//! popularity heuristic and tie-breaking for compatibility with Python difflib.
use std::collections::{BTreeMap, BTreeSet};

pub fn matching_blocks<T: Ord>(a: &[T], b: &[T]) -> Vec<(usize, usize, usize)> {
    matching_blocks_with_autojunk(a, b, true)
}
pub fn matching_blocks_with_autojunk<T: Ord>(
    a: &[T],
    b: &[T],
    autojunk: bool,
) -> Vec<(usize, usize, usize)> {
    let mut positions: BTreeMap<&T, Vec<usize>> = BTreeMap::new();
    for (i, value) in b.iter().enumerate() {
        positions.entry(value).or_default().push(i);
    }
    if autojunk && b.len() >= 200 {
        let limit = b.len() / 100 + 1;
        positions.retain(|_, v| v.len() <= limit);
    }
    let mut queue = vec![(0, a.len(), 0, b.len())];
    let mut found = vec![];
    while let Some((alo, ahi, blo, bhi)) = queue.pop() {
        let (mut best_a, mut best_b, mut size) = (alo, blo, 0);
        let mut previous = BTreeMap::new();
        for (i, value) in a.iter().enumerate().take(ahi).skip(alo) {
            let mut current = BTreeMap::new();
            if let Some(indices) = positions.get(value) {
                for j in indices.iter().copied().filter(|j| *j >= blo && *j < bhi) {
                    let k = 1 + j
                        .checked_sub(1)
                        .and_then(|j| previous.get(&j).copied())
                        .unwrap_or(0);
                    current.insert(j, k);
                    if k > size {
                        best_a = i + 1 - k;
                        best_b = j + 1 - k;
                        size = k;
                    }
                }
            }
            previous = current;
        }
        // Popular elements can extend a match even though they were removed
        // from the index. There is no junk predicate in these callers.
        while best_a > alo && best_b > blo && a[best_a - 1] == b[best_b - 1] {
            best_a -= 1;
            best_b -= 1;
            size += 1;
        }
        while best_a + size < ahi && best_b + size < bhi && a[best_a + size] == b[best_b + size] {
            size += 1;
        }
        if size > 0 {
            found.push((best_a, best_b, size));
            if alo < best_a && blo < best_b {
                queue.push((alo, best_a, blo, best_b));
            }
            if best_a + size < ahi && best_b + size < bhi {
                queue.push((best_a + size, ahi, best_b + size, bhi));
            }
        }
    }
    found.sort();
    let mut collapsed: Vec<(usize, usize, usize)> = vec![];
    for (a, b, size) in found {
        if let Some(last) = collapsed
            .last_mut()
            .filter(|(i, j, k)| *i + *k == a && *j + *k == b)
        {
            last.2 += size;
        } else {
            collapsed.push((a, b, size));
        }
    }
    collapsed.push((a.len(), b.len(), 0));
    collapsed
}

pub fn ratio<T: Ord>(a: &[T], b: &[T]) -> f64 {
    if a.is_empty() && b.is_empty() {
        return 1.0;
    }
    2.0 * matching_blocks(a, b)
        .iter()
        .map(|(_, _, n)| n)
        .sum::<usize>() as f64
        / (a.len() + b.len()) as f64
}

pub fn similar_functions(
    trace: &crate::trace::Trace,
    wanted: &str,
    limit: usize,
) -> Vec<(String, String)> {
    let wanted = wanted.to_lowercase();
    let wanted_chars: Vec<_> = wanted.chars().collect();
    let words: Vec<_> = wanted
        .split(|c: char| !c.is_ascii_lowercase() && !c.is_ascii_digit() && c != '_')
        .filter(|s| s.len() >= 2)
        .collect();
    let mut scores: Vec<_> = trace
        .allocations
        .iter()
        .map(|a| {
            let name = &a.before.function;
            let link = &a.before.link;
            let both = format!("{name} {link}").to_lowercase();
            let found = if words.is_empty() {
                0.0
            } else {
                words.iter().filter(|w| both.contains(**w)).count() as f64 / words.len() as f64
            };
            let ratio = ratio(
                &wanted_chars,
                &name.to_lowercase().chars().collect::<Vec<_>>(),
            );
            (found, ratio, name, link)
        })
        .collect();
    scores.sort_by(|a, b| b.0.total_cmp(&a.0).then_with(|| b.1.total_cmp(&a.1)));
    let mut seen = BTreeSet::new();
    scores
        .into_iter()
        .filter(|(_, _, name, link)| seen.insert((*name, *link)))
        .map(|(_, _, name, link)| (name.clone(), link.clone()))
        .take(limit)
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn python_difflib_ties_and_popularity() {
        assert_eq!(
            matching_blocks(b"abxcd", b"abcd"),
            [(0, 0, 2), (3, 2, 2), (5, 4, 0)]
        );
        assert_eq!(ratio(b"tide", b"diet"), 0.25);
        assert_eq!(ratio(b"diet", b"tide"), 0.5);
        let a = vec![b'a'; 300];
        let mut b = a.clone();
        b[150] = b'b';
        assert_eq!(matching_blocks(&a, &b), [(0, 0, 150), (300, 300, 0)]);
        assert_eq!(ratio::<u8>(&[], &[]), 1.0);
    }
}

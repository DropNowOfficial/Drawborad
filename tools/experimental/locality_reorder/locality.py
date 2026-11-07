"""Conservative native-bounds DAG scheduling, not semantic stroke planning."""
import math
import time
import numpy as np

MAX_EVENTS = 8192
MAX_SAMPLES = 500000
GRAPH_BYTES = 256 * 1024 * 1024
SCHEDULE_SECONDS = 120
LONG_JUMP = 100.


def check_deadline(deadline):
    if deadline is not None and time.perf_counter() >= deadline:
        raise TimeoutError('cooperative locality scheduling deadline')


def validate_metadata(metadata):
    if metadata.get('schemaVersion') != 1:
        raise ValueError('metadata schema')
    w, h = metadata.get('width'), metadata.get('height')
    if any(type(v) is not int or not 1 <= v <= 512 for v in (w, h)):
        raise ValueError('canvas bounds')
    events = metadata.get('events')
    if not isinstance(events, list) or not 1 <= len(events) <= MAX_EVENTS:
        raise ValueError('event budget')
    if metadata.get('eventCount') != len(events) or metadata.get('gapUs') != 80000:
        raise ValueError('event count or gap contract')
    total_samples = 0
    previous_key, previous_block = None, -1
    for i, event in enumerate(events):
        if event.get('ordinal') != i or type(event.get('block')) is not int:
            raise ValueError('original ordinal/block')
        key = (event.get('stage'), event.get('layerId'), event.get('objectId'))
        if not all(isinstance(s, str) and s for s in key):
            raise ValueError('phase/layer/object key')
        if key != previous_key:
            previous_block += 1
            previous_key = key
        if event['block'] != previous_block:
            raise ValueError('contiguous block metadata')
        box = event.get('bounds')
        if not isinstance(box, list) or len(box) != 4 or any(type(v) is not int for v in box):
            raise ValueError('native integer bbox')
        x0, y0, x1, y1 = box
        if not (0 <= x0 <= x1 <= w and 0 <= y0 <= y1 <= h):
            raise ValueError('clipped native bbox')
        for name in ('first', 'last'):
            p = event.get(name)
            if not isinstance(p, list) or len(p) != 2 or any(
                    isinstance(v, bool) or not isinstance(v, (int, float)) or not math.isfinite(v)
                    for v in p) or not (0 <= p[0] < w and 0 <= p[1] < h):
                raise ValueError('finite endpoints')
        n = event.get('sampleCount')
        if type(n) is not int or n <= 0:
            raise ValueError('sample count')
        total_samples += n
        if total_samples > MAX_SAMPLES:
            raise ValueError('sample budget')
    if metadata.get('sampleCount') != total_samples:
        raise ValueError('sample total')
    return events


def overlapping(box, others):
    """Half-open conservative boxes. No centerline or final visibility logic."""
    nonempty = (box[0] < box[2]) and (box[1] < box[3])
    return (nonempty & (others[:, 0] < others[:, 2]) & (others[:, 1] < others[:, 3]) &
            (box[0] < others[:, 2]) & (others[:, 0] < box[2]) &
            (box[1] < others[:, 3]) & (others[:, 1] < box[3]))


def travel(events, permutation):
    jumps = []
    grouped = {}
    for previous, current in zip(permutation, permutation[1:]):
        distance = math.dist(events[previous]['last'], events[current]['first'])
        jumps.append(distance)
        grouped.setdefault(events[current]['stage'], []).append(distance)
    phases = []
    for phase in dict.fromkeys(e['stage'] for e in events):
        values = np.asarray(grouped.get(phase, []), float)
        phases.append(dict(phase=phase, transitions=len(values),
                           total=float(values.sum()), median=float(np.median(values)) if len(values) else None,
                           p90=float(np.percentile(values, 90)) if len(values) else None,
                           maximum=float(values.max()) if len(values) else None,
                           longJumps=int(np.sum(values > LONG_JUMP)),
                           includesIncomingPhaseBoundary=True))
    return dict(total=float(sum(jumps)), transitions=len(jumps), phases=phases,
                longJumpThresholdPixels=LONG_JUMP, allBlockBoundariesIncluded=True,
                initialPointerPositionAssumed=False)


def validate_permutation(events, permutation, primer, deadline=None):
    n = len(events)
    if (not isinstance(permutation, list) or len(permutation) != n or
            any(type(i) is not int for i in permutation) or sorted(permutation) != list(range(n))):
        raise ValueError('permutation must be an ordinal bijection')
    if type(primer) is not int or not 0 <= primer <= n or permutation[:primer] != list(range(primer)):
        raise ValueError('protected primer prefix')
    if any(events[new]['block'] != events[old]['block'] for new, old in enumerate(permutation)):
        raise ValueError('cross-block permutation')
    position = np.empty(n, np.int32)
    position[permutation] = np.arange(n, dtype=np.int32)
    boxes = np.asarray([event['bounds'] for event in events], np.int64)
    blocks = np.array([event['block'] for event in events])
    conflicts = 0
    for i in range(n):
        check_deadline(deadline)
        later = np.flatnonzero((blocks[i+1:] == blocks[i]) & overlapping(boxes[i], boxes[i+1:])) + i + 1
        conflicts += len(later)
        if np.any(position[later] < position[i]):
            raise ValueError('native bbox dependency inverted')
    return int(conflicts)


def schedule_block(events, ordinals, previous_end, deadline, graph_bytes=GRAPH_BYTES):
    n = len(ordinals)
    columns = (n + 7) // 8
    # Stored graph/metadata arrays plus a conservative 64 B per entry for
    # each of three Python ordinal/output/ready-count lists. This is not RSS;
    # the input event JSON and short-lived vectorized masks are outside it.
    # No per-edge Python object is allocated.
    stored = n * columns + n * (4 + 1 + 4*8 + 4*8 + 3*64)
    if not 0 <= graph_bytes <= GRAPH_BYTES or stored > graph_bytes:
        raise ValueError('bounded graph storage budget')
    boxes = np.asarray([events[i]['bounds'] for i in ordinals], np.int64)
    starts = np.asarray([events[i]['first'] for i in ordinals], float)
    ends = np.asarray([events[i]['last'] for i in ordinals], float)
    successors = np.zeros((n, columns), np.uint8)
    indegree = np.zeros(n, np.int32)
    edges = 0
    graph_start = time.perf_counter()
    for i in range(n):
        check_deadline(deadline)
        conflicts = overlapping(boxes[i], boxes[i+1:])
        indegree[i+1:] += conflicts.astype(np.int32)
        row = np.zeros(n, np.uint8)
        row[i+1:] = conflicts
        successors[i] = np.packbits(row, bitorder='little')
        edges += int(conflicts.sum())
    graph_seconds = time.perf_counter() - graph_start
    ready = indegree == 0
    output, ready_sizes = [], []
    current = np.asarray(previous_end if previous_end is not None else starts[0], float)
    for _ in range(n):
        check_deadline(deadline)
        candidates = np.flatnonzero(ready)
        if not len(candidates):
            raise RuntimeError('forward dependency graph unexpectedly cyclic')
        ready_sizes.append(len(candidates))
        distances = np.sum(np.square(starts[candidates] - current), axis=1)
        chosen = int(candidates[int(np.argmin(distances))])
        ready[chosen] = False
        output.append(ordinals[chosen])
        current = ends[chosen]
        pending = np.flatnonzero(np.unpackbits(successors[chosen], count=n, bitorder='little'))
        indegree[pending] -= 1
        ready[pending[indegree[pending] == 0]] = True
    return output, dict(events=n, conflicts=edges, graphStoredBytes=stored,
                        graphScope='stored adjacency/indegree/readiness/bbox/endpoints plus conservative 3x64 bytes per ordinal for lists; excludes input JSON and transient masks; not total RSS',
                        graphSeconds=graph_seconds, totalSeconds=time.perf_counter()-graph_start,
                        readyMedian=float(np.median(ready_sizes)), readyMaximum=max(ready_sizes),
                        forcedSingleReady=int(np.sum(np.array(ready_sizes)==1)))


def schedule(metadata, primer, deadline=None, graph_bytes=GRAPH_BYTES):
    events = validate_metadata(metadata)
    if type(primer) is not int or not 0 <= primer <= len(events):
        raise ValueError('protected primer count')
    started = time.perf_counter()
    if deadline is None:
        deadline = started + SCHEDULE_SECONDS
    permutation = list(range(primer))
    blocks = []
    first = primer
    while first < len(events):
        end = first + 1
        while end < len(events) and events[end]['block'] == events[first]['block']:
            end += 1
        previous = events[permutation[-1]]['last'] if permutation else None
        order, stats = schedule_block(events, list(range(first,end)), previous, deadline, graph_bytes)
        permutation.extend(order)
        blocks.append(dict(first=first, end=end, block=events[first]['block'], **stats))
        first = end
    conflicts = validate_permutation(events, permutation, primer, deadline)
    baseline = travel(events, list(range(len(events))))
    raw = travel(events, permutation)
    retained = raw['total'] <= baseline['total']
    proposed = permutation.copy()
    if not retained:
        permutation = list(range(len(events)))
    return permutation, dict(
        originalTravel=baseline, proposedTravel=raw, retainedTravel=travel(events,permutation),
        rawCandidateRetained=retained, candidateFallbackReason=None if retained else 'total pen-up increased including block boundaries',
        rawCandidatePermutation=proposed, primer=primer, conflictsVerified=conflicts,
        blocks=blocks, seconds=time.perf_counter()-started,
        maxStoredGraphBytes=max((b['graphStoredBytes'] for b in blocks),default=0),
        graphBudgetBytes=GRAPH_BYTES, cacheIsNotTotalRSS=True,
        nearestReadyIsNotGlobalMinimum=True,
        ties='squared endpoint distance then original ordinal',
        noInitialPointerClaim='first unprotected start used only as deterministic seed if no primer; not counted in total')

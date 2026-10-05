namespace Pulse.Core.History;

/// <summary>Fixed-size ring of samples, oldest first when read.</summary>
public sealed class RingSeries
{
    readonly double[] _buf;
    int _start, _count;

    public RingSeries(int capacity) => _buf = new double[capacity];

    public int Capacity => _buf.Length;
    public int Count => _count;

    public void Push(double v)
    {
        if (_count < _buf.Length) _buf[(_start + _count++) % _buf.Length] = v;
        else { _buf[_start] = v; _start = (_start + 1) % _buf.Length; }
    }

    public double this[int i] => _buf[(_start + i) % _buf.Length];

    public double Last => _count == 0 ? 0 : this[_count - 1];

    public double[] ToArray()
    {
        var a = new double[_count];
        for (int i = 0; i < _count; i++) a[i] = this[i];
        return a;
    }

    public void Clear() { _start = 0; _count = 0; }
}

/// <summary>
/// A timeline of averaged buckets: every sample adds to the current bucket and
/// each closed bucket becomes one point. Pulse uses 5 s buckets and 121 points
/// for the 10-minute detail charts.
/// </summary>
public sealed class Timeline(TimeSpan bucket, int points)
{
    readonly RingSeries _points = new(points);
    DateTimeOffset _bucketStart = DateTimeOffset.MinValue;
    double _sum;
    int _n;

    public TimeSpan Bucket { get; } = bucket;
    public int Capacity => _points.Capacity;

    public void Add(DateTimeOffset at, double v)
    {
        if (_bucketStart == DateTimeOffset.MinValue) _bucketStart = at;
        while (at - _bucketStart >= Bucket)
        {
            if (_n > 0) { _points.Push(_sum / _n); _sum = 0; _n = 0; }
            else if (_points.Count > 0) _points.Push(_points.Last); // a gap repeats the last point
            _bucketStart += Bucket;
            if (at - _bucketStart > Bucket * Capacity) _bucketStart = at; // long sleep: restart
        }
        _sum += v;
        _n++;
    }

    /// <summary>Closed points plus the open bucket as the newest point.</summary>
    public double[] Points()
    {
        var closed = _points.ToArray();
        if (_n == 0) return closed;
        var all = new double[Math.Min(Capacity, closed.Length + 1)];
        int skip = closed.Length + 1 - all.Length;
        Array.Copy(closed, skip, all, 0, closed.Length - skip);
        all[^1] = _sum / _n;
        return all;
    }
}

/// <summary>Bytes moved since local midnight, from cumulative interface counters.</summary>
public sealed class DailyCounter
{
    ulong? _lastDown, _lastUp;
    DateOnly _day;

    public ulong Down { get; private set; }
    public ulong Up { get; private set; }

    public void Update(DateTime localNow, ulong cumulativeDown, ulong cumulativeUp)
    {
        var day = DateOnly.FromDateTime(localNow);
        if (day != _day) { _day = day; Down = 0; Up = 0; }
        // Counters reset when an adapter changes; a drop counts as a fresh start.
        if (_lastDown is ulong d && cumulativeDown >= d) Down += cumulativeDown - d;
        if (_lastUp is ulong u && cumulativeUp >= u) Up += cumulativeUp - u;
        _lastDown = cumulativeDown;
        _lastUp = cumulativeUp;
    }
}

/// <summary>Turns two cumulative counter readings into a per-second rate.</summary>
public sealed class RateMeter
{
    ulong? _last;
    long _lastTicks;

    public double Update(ulong cumulative, long timestampTicks, long ticksPerSecond)
    {
        double rate = 0;
        if (_last is ulong prev && timestampTicks > _lastTicks && cumulative >= prev)
            rate = (cumulative - prev) * (double)ticksPerSecond / (timestampTicks - _lastTicks);
        _last = cumulative;
        _lastTicks = timestampTicks;
        return rate;
    }
}

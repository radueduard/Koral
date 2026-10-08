namespace Koral.Net;

/// <summary>knet::TickClock: a fixed-rate simulation clock.</summary>
public sealed class TickClock(double ticksPerSecond = 60.0, int maxTicksPerAdvance = 8)
{
    private readonly double _interval = 1.0 / ticksPerSecond;
    private double _accumulator;

    public uint Tick { get; private set; }
    public double Interval => _interval;
    /// <summary>How far into the next tick, 0..1.</summary>
    public float Alpha => (float)(_accumulator / _interval);

    /// <summary>Adds real time; returns how many ticks are due (capped: a long stall is dropped, not replayed).</summary>
    public int Advance(double seconds)
    {
        _accumulator += Math.Max(0.0, seconds);
        int ticks = (int)(_accumulator / _interval);
        if (ticks > maxTicksPerAdvance)
        {
            ticks = maxTicksPerAdvance;
            _accumulator = 0.0;
        }
        else
        {
            _accumulator -= ticks * _interval;
        }
        Tick += (uint)ticks;
        return ticks;
    }
    public void Reset(uint tick) { Tick = tick; _accumulator = 0.0; }
}

/// <summary>knet::Predictor: the local state run ahead on local input, corrected by the server's.</summary>
public sealed class Predictor<TState, TInput>(TState initial, Func<TState, TInput, TState> step, int maxPending = 256)
{
    private readonly LinkedList<(uint Tick, TInput Input)> _pending = new();

    public TState Current { get; private set; } = initial;
    public uint Tick { get; private set; }
    public uint Confirmed { get; private set; }
    public ulong Reconciliations { get; private set; }
    /// <summary>Inputs the server has not confirmed: send these each tick.</summary>
    public IReadOnlyCollection<(uint Tick, TInput Input)> Pending => _pending;

    /// <summary>Runs <paramref name="input"/> for the next tick, at once; returns that tick.</summary>
    public uint Apply(TInput input)
    {
        uint tick = ++Tick;
        _pending.AddLast((tick, input));
        if (_pending.Count > maxPending) _pending.RemoveFirst();
        Current = step(Current, input);
        return tick;
    }

    /// <summary>The server's state after its tick <paramref name="tick"/>: rewind to it, replay what came after.</summary>
    public void Reconcile(uint tick, TState authoritative)
    {
        if (tick < Confirmed) return;
        Confirmed = tick;
        while (_pending.First is { } first && first.Value.Tick <= tick) _pending.RemoveFirst();
        var state = authoritative;
        foreach (var (_, input) in _pending) state = step(state, input);
        Current = state;
        ++Reconciliations;
    }
}

/// <summary>knet::InputBuffer: a client's inputs by tick, handed out in order; a missing one repeats the last.</summary>
public sealed class InputBuffer<TInput>(int capacity = 256)
{
    private readonly SortedDictionary<uint, TInput> _inputs = new();
    private bool _taken, _hasLast;
    private uint _lastTaken;
    private TInput? _last;

    public ulong Missed { get; private set; }
    public ulong Received { get; private set; }
    public int Count => _inputs.Count;
    public uint? NewestTick => _inputs.Count == 0 ? null : _inputs.Keys.Last();

    public void Add(uint tick, TInput input)
    {
        if (_taken && tick <= _lastTaken) return;
        _inputs.TryAdd(tick, input);
        while (_inputs.Count > capacity) _inputs.Remove(_inputs.Keys.First());
    }

    /// <summary>The input for <paramref name="tick"/>, or the last one taken; false before any.</summary>
    public bool Take(uint tick, out TInput input)
    {
        foreach (var old in _inputs.Keys.TakeWhile(k => k < tick).ToList()) _inputs.Remove(old);
        _taken = true;
        _lastTaken = tick;
        if (_inputs.Remove(tick, out var found))
        {
            _last = found;
            _hasLast = true;
            ++Received;
        }
        else
        {
            ++Missed;
        }
        input = _last!;
        return _hasLast;
    }
}

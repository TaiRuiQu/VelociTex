using System.Diagnostics;

internal static class BatchTiming
{
    internal sealed record Config(int Samples = 15, double BatchMs = 25, double WarmupMs = 200);
    internal sealed record Batch(int Operations, double TotalMs, double MsPerOperation);
    internal sealed record Summary(int Samples, double MedianMs, double MeanMs, double MinimumMs, double MaximumMs);
    internal sealed record Measurement(int OperationsPerBatch, List<Batch> Batches, Summary Summary);

    internal static Measurement Measure(Action action, Config config)
    {
        double Run(int count)
        {
            long start = Stopwatch.GetTimestamp();
            for (int i = 0; i < count; i++)
                action();
            return Stopwatch.GetElapsedTime(start).TotalMilliseconds;
        }
        int count = 1;
        while (Run(count) < config.BatchMs)
            count = checked(count * 2);
        var batches = new List<Batch>();
        for (int i = 0; i < config.Samples; i++)
        {
            double total = Run(count);
            batches.Add(new(count, total, total / count));
        }
        double[] values = batches.Select(b => b.MsPerOperation).Order().ToArray();
        return new(count, batches, new(values.Length, values[values.Length / 2], values.Average(), values[0], values[^1]));
    }
}

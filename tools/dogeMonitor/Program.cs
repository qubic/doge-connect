using Microsoft.Extensions.Configuration;
using Qubic.Core.Entities;
using Qubic.Doge.Monitor;

Console.WriteLine("=== Doge Dispatcher Monitor ===");
Console.WriteLine();

var switchMappings = new Dictionary<string, string>
{
    { "--port", "DogeMonitor:Port" },
    { "-p", "DogeMonitor:Port" },
    { "--dispatcher", "DogeMonitor:DispatcherIdentity" },
    { "-d", "DogeMonitor:DispatcherIdentity" },
};

var config = new ConfigurationBuilder()
    .SetBasePath(Directory.GetCurrentDirectory())
    .AddJsonFile("appsettings.json", optional: true)
    .AddCommandLine(args, switchMappings)
    .Build();

var section = config.GetSection("DogeMonitor");
var dispatcherIdentity = section["DispatcherIdentity"] ?? "XPILPIJYHRBTACMMIRSJLIZWCXDBHWVEOTZBQFBXWEUXDZGGDEKDQPIEQKQK";
var port = int.Parse(section["Port"] ?? "21841");

// collect peers from config + bare CLI args
var flagValues = new HashSet<string>();
for (int i = 0; i < args.Length; i++)
{
    if (args[i].StartsWith("-") && i + 1 < args.Length)
    {
        flagValues.Add(args[i + 1]);
        i++;
    }
}

var peers = section.GetSection("Peers").Get<string[]>() ?? Array.Empty<string>();
var cliPeers = args
    .Where(a => !a.StartsWith("-") && !a.StartsWith("/") && a.Contains('.') && !flagValues.Contains(a))
    .ToList();
var allPeers = peers.Concat(cliPeers).Distinct().ToList();

if (allPeers.Count == 0)
{
    Console.ForegroundColor = ConsoleColor.Red;
    Console.WriteLine("No peers specified. Usage:");
    Console.ResetColor();
    Console.WriteLine("  dotnet run -- <ip1> <ip2> ... [--port <port>]");
    Console.WriteLine();
    Console.WriteLine("Options:");
    Console.WriteLine("  -p, --port <port>          Peer port (default: 21841)");
    Console.WriteLine("  -d, --dispatcher <id>      Dispatcher Qubic identity");
    Console.WriteLine();
    Console.WriteLine("  Or configure in appsettings.json under \"DogeMonitor\"");
    return;
}

// resolve dispatcher public key from identity using Qubic.NET
var identity = QubicIdentity.FromIdentity(dispatcherIdentity);
var dispatcherPublicKey = identity.PublicKey;

Console.WriteLine($"Dispatcher: {dispatcherIdentity}");
Console.WriteLine($"Public Key: {Convert.ToHexString(dispatcherPublicKey)}");
Console.WriteLine($"Port:       {port}");
Console.WriteLine($"Peers:      {string.Join(", ", allPeers)}");
Console.WriteLine();
Console.WriteLine("Listening for DOGE mining tasks... (Ctrl+C to stop, 's' for stats)");
Console.WriteLine(new string('-', 60));
Console.WriteLine();

var connections = new List<DogeNodeConnection>();
var cts = new CancellationTokenSource();

Console.CancelKeyPress += (_, e) =>
{
    e.Cancel = true;
    cts.Cancel();
};

foreach (var peer in allPeers)
{
    var conn = new DogeNodeConnection(peer, dispatcherPublicKey, port);
    connections.Add(conn);
    _ = conn.StartAsync(cts.Token);
}

// input loop for stats
_ = Task.Run(() =>
{
    while (!cts.Token.IsCancellationRequested)
    {
        try
        {
            if (Console.KeyAvailable)
            {
                var key = Console.ReadKey(true);
                if (key.KeyChar is 's' or 'S')
                {
                    Console.WriteLine();
                    Console.WriteLine("--- Connection Stats ---");
                    foreach (var c in connections)
                        c.PrintStats();
                    Console.WriteLine("------------------------");
                    Console.WriteLine();
                }
            }
        }
        catch { }
        Thread.Sleep(100);
    }
}, cts.Token);

try
{
    await Task.Delay(Timeout.Infinite, cts.Token);
}
catch (OperationCanceledException) { }

Console.WriteLine();
Console.WriteLine("Shutting down...");
foreach (var conn in connections)
    conn.Dispose();

using Microsoft.Extensions.Configuration;
using Qubic.Doge.NetBridge;

Console.WriteLine("=== Qubic DOGE Net Bridge ===");
Console.WriteLine("One-way relay: mainnet → testnet (tasks + solutions)");
Console.WriteLine();

var switchMappings = new Dictionary<string, string>
{
    { "--mainnet-port", "Bridge:MainnetPort" },
    { "--testnet-port", "Bridge:TestnetPort" },
};

var config = new ConfigurationBuilder()
    .SetBasePath(Directory.GetCurrentDirectory())
    .AddJsonFile("appsettings.json", optional: true)
    .AddCommandLine(args, switchMappings)
    .Build();

var section = config.GetSection("Bridge");
var mainnetPort = int.Parse(section["MainnetPort"] ?? "21841");
var testnetPort = int.Parse(section["TestnetPort"] ?? "21841");
var deduplicationWindow = int.Parse(section["DeduplicationWindow"] ?? "2000");
var statsIntervalSec = int.Parse(section["StatsIntervalSec"] ?? "10");

var mainnetPeers = section.GetSection("MainnetPeers").Get<string[]>() ?? Array.Empty<string>();
var testnetPeers = section.GetSection("TestnetPeers").Get<string[]>() ?? Array.Empty<string>();

// Also accept bare CLI args: first group before "--" = mainnet, after "--" = testnet.
// Example: dotnet run -- 1.2.3.4 5.6.7.8 -- 10.0.0.1 10.0.0.2
var bareArgs = args.Where(a => !a.StartsWith("-") && a.Contains('.')).ToList();
var separatorIdx = Array.IndexOf(args, "--");
if (separatorIdx >= 0 && mainnetPeers.Length == 0 && testnetPeers.Length == 0)
{
    var beforeSep = args.Take(separatorIdx).Where(a => !a.StartsWith("-") && a.Contains('.')).ToArray();
    var afterSep = args.Skip(separatorIdx + 1).Where(a => !a.StartsWith("-") && a.Contains('.')).ToArray();
    if (beforeSep.Length > 0) mainnetPeers = beforeSep;
    if (afterSep.Length > 0) testnetPeers = afterSep;
}

if (mainnetPeers.Length == 0 || testnetPeers.Length == 0)
{
    Console.ForegroundColor = ConsoleColor.Red;
    Console.WriteLine("Both mainnet and testnet peers are required.");
    Console.ResetColor();
    Console.WriteLine();
    Console.WriteLine("Usage:");
    Console.WriteLine("  dotnet run -- <mainnet-ip1> <mainnet-ip2> -- <testnet-ip1> <testnet-ip2>");
    Console.WriteLine();
    Console.WriteLine("Or configure in appsettings.json:");
    Console.WriteLine("  { \"Bridge\": { \"MainnetPeers\": [...], \"TestnetPeers\": [...] } }");
    Console.WriteLine();
    Console.WriteLine("Options:");
    Console.WriteLine("  --mainnet-port <port>   Mainnet peer port (default: 21841)");
    Console.WriteLine("  --testnet-port <port>   Testnet peer port (default: 21841)");
    return;
}

Console.WriteLine($"Mainnet:  {mainnetPeers.Length} peers (port {mainnetPort})");
foreach (var p in mainnetPeers) Console.WriteLine($"  - {p}");
Console.WriteLine($"Testnet:  {testnetPeers.Length} peers (port {testnetPort})");
foreach (var p in testnetPeers) Console.WriteLine($"  - {p}");
Console.WriteLine($"Dedup:    {deduplicationWindow} window");
Console.WriteLine();

var cts = new CancellationTokenSource();
Console.CancelKeyPress += (_, e) => { e.Cancel = true; cts.Cancel(); };

using var bridge = new BridgeRelay(mainnetPeers, mainnetPort, testnetPeers, testnetPort, deduplicationWindow);

Console.WriteLine("Connecting...");
await bridge.StartAsync(cts.Token);

Console.WriteLine();
Console.WriteLine("Bridge running. Press Ctrl+C to stop, 's' for stats.");
Console.WriteLine(new string('-', 60));

// Stats timer
using var statsTimer = new Timer(_ => bridge.PrintStats(), null,
    TimeSpan.FromSeconds(statsIntervalSec), TimeSpan.FromSeconds(statsIntervalSec));

// Input loop
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
                    bridge.PrintStats();
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
Console.WriteLine("Bridge stopped.");
bridge.PrintStats();

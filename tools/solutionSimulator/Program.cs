using Microsoft.Extensions.Configuration;
using Qubic.Core.Entities;
using Qubic.Crypto;
using Qubic.Doge.SolutionSimulator;

Console.WriteLine("=== Doge Solution Simulator ===");
Console.WriteLine();

var switchMappings = new Dictionary<string, string>
{
    { "--port", "DogeMonitor:Port" },
    { "-p", "DogeMonitor:Port" },
    { "--seed", "Sender:Seed" },
    { "-s", "Sender:Seed" },
    { "--job-id", "Sender:JobId" },
    { "-j", "Sender:JobId" },
    { "--count", "Sender:Count" },
    { "-c", "Sender:Count" },
    { "--interval", "Sender:IntervalMs" },
    { "-i", "Sender:IntervalMs" },
};

var config = new ConfigurationBuilder()
    .SetBasePath(Directory.GetCurrentDirectory())
    .AddJsonFile("appsettings.json", optional: true)
    .AddCommandLine(args, switchMappings)
    .Build();

var monitorSection = config.GetSection("DogeMonitor");
var senderSection = config.GetSection("Sender");

var port = int.Parse(monitorSection["Port"] ?? "21841");
var seed = senderSection["Seed"];
var jobId = ulong.Parse(senderSection["JobId"] ?? "0");
var count = int.Parse(senderSection["Count"] ?? "1");
var intervalMs = int.Parse(senderSection["IntervalMs"] ?? "2000");

if (string.IsNullOrWhiteSpace(seed) || seed.Length != 55)
{
    Console.ForegroundColor = ConsoleColor.Red;
    Console.WriteLine("A valid 55-character seed is required.");
    Console.ResetColor();
    Console.WriteLine();
    Console.WriteLine("Usage:");
    Console.WriteLine("  dotnet run -- --seed <55-char-seed> <ip1> <ip2> ... [options]");
    Console.WriteLine();
    Console.WriteLine("Options:");
    Console.WriteLine("  -s, --seed <seed>          Qubic seed (55 lowercase chars, REQUIRED)");
    Console.WriteLine("  -p, --port <port>          Peer port (default: 21841)");
    Console.WriteLine("  -j, --job-id <id>          Demo job ID (default: current unix timestamp)");
    Console.WriteLine("  -c, --count <n>            Number of solutions to send (default: 1)");
    Console.WriteLine("  -i, --interval <ms>        Interval between sends in ms (default: 2000)");
    Console.WriteLine();
    Console.WriteLine("  Or configure in appsettings.json");
    return;
}

// collect peers
var flagValues = new HashSet<string>();
for (int i = 0; i < args.Length; i++)
{
    if (args[i].StartsWith("-") && i + 1 < args.Length)
    {
        flagValues.Add(args[i + 1]);
        i++;
    }
}

var peers = monitorSection.GetSection("Peers").Get<string[]>() ?? Array.Empty<string>();
var cliPeers = args
    .Where(a => !a.StartsWith("-") && !a.StartsWith("/") && a.Contains('.') && !flagValues.Contains(a))
    .ToList();
var allPeers = peers.Concat(cliPeers).Distinct().ToList();

if (allPeers.Count == 0)
{
    Console.ForegroundColor = ConsoleColor.Red;
    Console.WriteLine("No peers specified. Provide peer IPs as arguments or in appsettings.json.");
    Console.ResetColor();
    return;
}

// resolve sender identity/pubkey from seed using Qubic.NET
var crypt = new QubicCrypt();
var senderIdentity = crypt.GetIdentityFromSeed(seed);
var identity = QubicIdentity.FromIdentity(senderIdentity);
var senderPublicKey = identity.PublicKey;

Console.WriteLine($"Sender ID:  {senderIdentity}");
Console.WriteLine($"Public Key: {Convert.ToHexString(senderPublicKey)[..32]}...");
Console.WriteLine($"Port:       {port}");
Console.WriteLine($"Peers:      {string.Join(", ", allPeers)}");
Console.WriteLine($"Job ID:     {(jobId == 0 ? "(auto: unix timestamp)" : jobId.ToString())}");
Console.WriteLine($"Count:      {count}");
Console.WriteLine($"Interval:   {intervalMs}ms");
Console.WriteLine(new string('-', 60));
Console.WriteLine();

var senders = new List<DogeSolutionSender>();
var cts = new CancellationTokenSource();

foreach (var peer in allPeers)
{
    var sender = new DogeSolutionSender(peer, port, seed, senderPublicKey);
    senders.Add(sender);
    await sender.ConnectAsync(cts.Token);
}

await Task.Delay(2000);

var connected = senders.Where(s => s.IsConnected).ToList();
if (connected.Count == 0)
{
    Console.ForegroundColor = ConsoleColor.Red;
    Console.WriteLine("Failed to connect to any peer. Exiting.");
    Console.ResetColor();
    foreach (var s in senders) s.Dispose();
    return;
}

Console.WriteLine($"Connected to {connected.Count}/{senders.Count} peers.");
Console.WriteLine();

for (int i = 0; i < count; i++)
{
    var currentJobId = jobId + (ulong)i;
    Console.WriteLine($"--- Sending solution {i + 1}/{count} (jobId={currentJobId}) ---");

    foreach (var sender in connected)
    {
        try
        {
            await sender.SendDemoSolutionAsync(currentJobId, cts.Token);
        }
        catch (Exception ex)
        {
            Console.ForegroundColor = ConsoleColor.Red;
            Console.WriteLine($"Error sending: {ex.Message}");
            Console.ResetColor();
        }
    }

    if (i < count - 1)
    {
        Console.WriteLine($"Waiting {intervalMs}ms...");
        await Task.Delay(intervalMs);
    }
}

Console.WriteLine();
Console.WriteLine("All solutions sent. Waiting 2s for responses...");
await Task.Delay(2000);

Console.WriteLine("Done.");
foreach (var s in senders) s.Dispose();

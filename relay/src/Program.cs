using Qubic.Doge.Relay;

var builder = WebApplication.CreateBuilder(args);

builder.Services.AddSingleton<ClientRegistry>();
builder.Services.AddHostedService<RedisSubscriberService>();
builder.Logging.ClearProviders().AddSimpleConsole(o =>
{
    o.TimestampFormat = "[yyyy-MM-dd HH:mm:ss.fff] ";
    o.SingleLine = true;
});

var app = builder.Build();

// Configuration (env overrides via RELAY__ prefix)
var redisUrl = builder.Configuration["Redis:ConnectionString"] ?? "localhost:6379";
var channelName = builder.Configuration["Redis:Channel"] ?? "doge:shares";
var ringBufferSize = int.TryParse(builder.Configuration["Relay:RingBufferSize"], out var rb) ? rb : 100;
var maxClients = int.TryParse(builder.Configuration["Relay:MaxClients"], out var mc) ? mc : 5000;

app.Logger.LogInformation("Config: redis={Url} channel={Channel} ringBuffer={Rb} maxClients={Mc}",
    redisUrl, channelName, ringBufferSize, maxClients);

app.UseWebSockets(new WebSocketOptions
{
    KeepAliveInterval = TimeSpan.FromSeconds(30)
});

// Health check
app.MapGet("/health", (ClientRegistry reg) => Results.Ok(new
{
    status = "ok",
    connectedClients = reg.Count,
    uptimeSeconds = (DateTime.UtcNow - Process.StartTime.ToUniversalTime()).TotalSeconds
}));

// WebSocket endpoint for clients
app.Map("/ws", async (HttpContext ctx, ClientRegistry reg, ILogger<Program> logger) =>
{
    if (!ctx.WebSockets.IsWebSocketRequest)
    {
        ctx.Response.StatusCode = 400;
        await ctx.Response.WriteAsync("WebSocket required");
        return;
    }

    if (reg.Count >= maxClients)
    {
        ctx.Response.StatusCode = 503;
        await ctx.Response.WriteAsync("Max clients reached");
        return;
    }

    using var ws = await ctx.WebSockets.AcceptWebSocketAsync();
    var clientId = Guid.NewGuid();
    var remote = ctx.Connection.RemoteIpAddress?.ToString() ?? "?";
    logger.LogInformation("Client connected: {Id} from {Remote}", clientId, remote);

    reg.Register(clientId, ws);

    // Send recent events from ring buffer so the client has immediate history.
    foreach (var recent in reg.GetRecent())
    {
        try
        {
            await ws.SendAsync(recent, System.Net.WebSockets.WebSocketMessageType.Text, true, ctx.RequestAborted);
        }
        catch { break; }
    }

    // Keep the connection open; we don't expect incoming messages.
    var buffer = new byte[1024];
    try
    {
        while (ws.State == System.Net.WebSockets.WebSocketState.Open && !ctx.RequestAborted.IsCancellationRequested)
        {
            var result = await ws.ReceiveAsync(buffer, ctx.RequestAborted);
            if (result.MessageType == System.Net.WebSockets.WebSocketMessageType.Close)
                break;
        }
    }
    catch { /* client disconnected */ }
    finally
    {
        reg.Unregister(clientId);
        logger.LogInformation("Client disconnected: {Id}", clientId);
    }
});

app.Run();

public static class Process
{
    public static DateTime StartTime { get; } = DateTime.UtcNow;
}

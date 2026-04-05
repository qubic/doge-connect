using StackExchange.Redis;

namespace Qubic.Doge.Relay;

/// <summary>
/// Subscribes to the Redis pub/sub channel and broadcasts each message to all WS clients.
/// Auto-reconnects to Redis if the connection drops.
/// </summary>
public class RedisSubscriberService : BackgroundService
{
    private readonly ClientRegistry _clients;
    private readonly ILogger<RedisSubscriberService> _logger;
    private readonly string _redisUrl;
    private readonly string _channel;

    public RedisSubscriberService(ClientRegistry clients, IConfiguration config, ILogger<RedisSubscriberService> logger)
    {
        _clients = clients;
        _logger = logger;
        _redisUrl = config["Redis:ConnectionString"] ?? "localhost:6379";
        _channel = config["Redis:Channel"] ?? "doge:shares";
    }

    protected override async Task ExecuteAsync(CancellationToken ct)
    {
        while (!ct.IsCancellationRequested)
        {
            try
            {
                var options = ConfigurationOptions.Parse(_redisUrl);
                options.AbortOnConnectFail = false;
                options.ConnectRetry = 3;
                using var mux = await ConnectionMultiplexer.ConnectAsync(options);
                _logger.LogInformation("Connected to Redis at {Url}", _redisUrl);

                var sub = mux.GetSubscriber();
                var taskComplete = new TaskCompletionSource();

                mux.ConnectionFailed += (_, e) =>
                {
                    _logger.LogWarning("Redis connection failed: {Ex}", e.Exception?.Message);
                    taskComplete.TrySetResult();
                };

                await sub.SubscribeAsync(RedisChannel.Literal(_channel), async (_, msg) =>
                {
                    if (msg.IsNullOrEmpty) return;
                    var bytes = System.Text.Encoding.UTF8.GetBytes((string)msg!);
                    try
                    {
                        await _clients.BroadcastAsync(bytes, ct);
                    }
                    catch (Exception ex)
                    {
                        _logger.LogError(ex, "Broadcast failed");
                    }
                });

                _logger.LogInformation("Subscribed to channel '{Channel}'", _channel);

                // Wait until cancelled or connection fails.
                using (ct.Register(() => taskComplete.TrySetResult()))
                    await taskComplete.Task;

                await sub.UnsubscribeAsync(RedisChannel.Literal(_channel));
            }
            catch (Exception ex)
            {
                _logger.LogError(ex, "Redis subscriber error, reconnecting in 5s");
                await Task.Delay(5000, ct);
            }
        }
    }
}

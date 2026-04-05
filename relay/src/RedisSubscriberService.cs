using StackExchange.Redis;

namespace Qubic.Doge.Relay;

public class RedisSubscriberService : BackgroundService
{
    private readonly ClientRegistry _clients;
    private readonly ILogger<RedisSubscriberService> _logger;
    private readonly string _redisUrl;
    private readonly string _channel;

    public RedisSubscriberService(
        ClientRegistry clients,
        IConfiguration config,
        ILogger<RedisSubscriberService> logger)
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
            ConnectionMultiplexer? mux = null;

            try
            {
                var options = ConfigurationOptions.Parse(_redisUrl);
                options.AbortOnConnectFail = false;
                options.ConnectRetry = 3;
                options.ResolveDns = true;
                options.ConnectTimeout = 15000;
                options.SyncTimeout = 15000;
                options.AsyncTimeout = 15000;
                options.KeepAlive = 30;

                mux = await ConnectionMultiplexer.ConnectAsync(options);

                mux.ConnectionFailed += (_, e) =>
                {
                    _logger.LogWarning(e.Exception,
                        "Redis connection failed. Endpoint={Endpoint}, Type={Type}, FailureType={FailureType}",
                        e.EndPoint, e.ConnectionType, e.FailureType);
                };

                mux.ConnectionRestored += (_, e) =>
                {
                    _logger.LogInformation(
                        "Redis connection restored. Endpoint={Endpoint}, Type={Type}",
                        e.EndPoint, e.ConnectionType);
                };

                mux.ErrorMessage += (_, e) =>
                {
                    _logger.LogWarning("Redis error message: {Message}", e.Message);
                };

                mux.InternalError += (_, e) =>
                {
                    _logger.LogError(e.Exception, "Redis internal error");
                };

                var sub = mux.GetSubscriber();

                var queue = await sub.SubscribeAsync(RedisChannel.Literal(_channel));

                _logger.LogInformation(
                    "Redis multiplexer created. IsConnected={IsConnected}. Subscribed to channel '{Channel}'",
                    mux.IsConnected, _channel);

                queue.OnMessage(async channelMessage =>
                {
                    if (channelMessage.Message.IsNullOrEmpty) return;

                    try
                    {
                        var bytes = System.Text.Encoding.UTF8.GetBytes((string)channelMessage.Message!);
                        await _clients.BroadcastAsync(bytes, ct);
                    }
                    catch (Exception ex)
                    {
                        _logger.LogError(ex, "Broadcast failed");
                    }
                });

                try
                {
                    await Task.Delay(Timeout.InfiniteTimeSpan, ct);
                }
                catch (OperationCanceledException) when (ct.IsCancellationRequested)
                {
                }

                await sub.UnsubscribeAsync(RedisChannel.Literal(_channel));
            }
            catch (OperationCanceledException) when (ct.IsCancellationRequested)
            {
                break;
            }
            catch (Exception ex)
            {
                _logger.LogError(ex, "Redis subscriber error, reconnecting in 5s");

                try
                {
                    await Task.Delay(TimeSpan.FromSeconds(5), ct);
                }
                catch (OperationCanceledException) when (ct.IsCancellationRequested)
                {
                    break;
                }
            }
            finally
            {
                if (mux is not null)
                    await mux.CloseAsync();
                mux?.Dispose();
            }
        }
    }
}
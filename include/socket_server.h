#ifndef QTNG_SOCKET_SERVER_H
#define QTNG_SOCKET_SERVER_H

#include "kcp.h"
#include "socket_utils.h"
#include "coroutine_utils.h"
#ifndef QTNG_NO_CRYPTO
#include "ssl.h"
#endif

QTNETWORKNG_NAMESPACE_BEGIN

class BaseStreamServerPrivate;
class BaseStreamServer
{
public:
    BaseStreamServer(const QHostAddress &serverAddress, quint16 serverPort);
    BaseStreamServer(quint16 serverPort) : BaseStreamServer(QHostAddress::Any, serverPort) {}
    virtual ~BaseStreamServer();
protected:
    // these two virtual functions should be overrided by subclass.
    virtual QSharedPointer<SocketLike> serverCreate() = 0;
    virtual void processRequest(QSharedPointer<SocketLike> request) = 0;
public:
    bool allowReuseAddress() const;                    // default to true,
    void setAllowReuseAddress(bool b);
    int requestQueueSize() const;                      // default to 100
    void setRequestQueueSize(int requestQueueSize);
    bool serveForever();                               // serve blocking
    bool start();                                      // serve in background
    void stop();                                       // stop serving
    virtual bool isSecure() const;                     // is this ssl?
public:
    void setUserData(void *data); // the owner of data is not changed.
    void *userData() const;
public:
    quint16 serverPort() const;
    QHostAddress serverAddress() const;
public:
    QSharedPointer<Event> started;
    QSharedPointer<Event> stopped;
protected:
    virtual bool serverBind();                          // bind()
    virtual bool serverActivate();                      // listen()
    virtual void serverClose();                         // close()
    virtual bool serviceActions();                      // default to nothing, called before accept next request.
    virtual QSharedPointer<SocketLike> getRequest();    // accept();
    virtual QSharedPointer<SocketLike> prepareRequest(QSharedPointer<SocketLike> request);  // ssl handshake, default to nothing for tcp
    virtual bool verifyRequest(QSharedPointer<SocketLike> request);
    virtual void handleError(QSharedPointer<SocketLike> request);
    virtual void shutdownRequest(QSharedPointer<SocketLike> request);
    virtual void closeRequest(QSharedPointer<SocketLike> request);
protected:
    BaseStreamServerPrivate * const d_ptr;
    BaseStreamServer(BaseStreamServerPrivate *d);
private:
    Q_DECLARE_PRIVATE(BaseStreamServer)
};


template<typename RequestHandler>
class TcpServer: public BaseStreamServer
{
public:
    TcpServer(const QHostAddress &serverAddress, quint16 serverPort)
        : BaseStreamServer(serverAddress, serverPort) {}
    TcpServer(quint16 serverPort)
        : BaseStreamServer(QHostAddress::Any, serverPort) {}
protected:
    virtual QSharedPointer<SocketLike> serverCreate() override;
    virtual void processRequest(QSharedPointer<SocketLike> request) override;
};


template<typename RequestHandler>
QSharedPointer<SocketLike> TcpServer<RequestHandler>::serverCreate()
{
    return asSocketLike(Socket::createServer(serverAddress(), serverPort(), 0));
}


template<typename RequestHandler>
void TcpServer<RequestHandler>::processRequest(QSharedPointer<SocketLike> request)
{
    RequestHandler handler;
    handler.request = request;
    handler.server = this;
    handler.run();
}


template<typename RequestHandler>
class KcpServer: public BaseStreamServer
{
public:
    KcpServer(const QHostAddress &serverAddress, quint16 serverPort)
        :BaseStreamServer(serverAddress, serverPort) {}
    KcpServer(quint16 serverPort)
        : BaseStreamServer(QHostAddress::Any, serverPort) {}
protected:
    virtual QSharedPointer<SocketLike> serverCreate() override;
    virtual void processRequest(QSharedPointer<SocketLike> request) override;
};


template<typename RequestHandler>
QSharedPointer<SocketLike> KcpServer<RequestHandler>::serverCreate()
{
    return asSocketLike(KcpSocket::createServer(serverAddress(), serverPort(), 0));
}


template<typename RequestHandler>
void KcpServer<RequestHandler>::processRequest(QSharedPointer<SocketLike> request)
{
    RequestHandler handler;
    handler.request = request;
    handler.server = this;
    handler.run();
}


#ifndef QTNG_NO_CRYPTO


template<typename ServerType>
class WithSsl: public ServerType
{
public:
    WithSsl(const QHostAddress &serverAddress, quint16 serverPort);
    WithSsl(const QHostAddress &serverAddress, quint16 serverPort, const SslConfiguration &configuration);
    WithSsl(quint16 serverPort);
    WithSsl(quint16 serverPort, const SslConfiguration &configuration);
public:
    void setSslConfiguration(const SslConfiguration &configuration);
    SslConfiguration sslConfiguration() const;
    void setSslHandshakeTimeout(float sslHandshakeTimeout);
    float sslHandshakeTimeout() const;
    virtual bool isSecure() const override;
protected:
    virtual QSharedPointer<SocketLike> prepareRequest(QSharedPointer<SocketLike> request) override;
private:
    SslConfiguration _configuration;
    float _sslHandshakeTimeout;
};


template<typename ServerType>
WithSsl<ServerType>::WithSsl(const QHostAddress &serverAddress, quint16 serverPort)
    : ServerType(serverAddress, serverPort)
    , _sslHandshakeTimeout(5.0)
{
    _configuration = SslConfiguration::testPurpose("SslServer", "CN", "QtNetworkNg");
}


template<typename ServerType>
WithSsl<ServerType>::WithSsl(const QHostAddress &serverAddress, quint16 serverPort, const SslConfiguration &configuration)
    : ServerType(serverAddress, serverPort)
    , _configuration(configuration)
    , _sslHandshakeTimeout(5.0)
{}


template<typename ServerType>
WithSsl<ServerType>::WithSsl(quint16 serverPort)
    : ServerType(QHostAddress::Any, serverPort)
    , _sslHandshakeTimeout(5.0)
{
    _configuration = SslConfiguration::testPurpose("SslServer", "CN", "QtNetworkNg");
}


template<typename ServerType>
WithSsl<ServerType>::WithSsl(quint16 serverPort, const SslConfiguration &configuration)
    : ServerType(QHostAddress::Any, serverPort)
    , _configuration(configuration)
    , _sslHandshakeTimeout(5.0)
{}


template<typename ServerType>
void WithSsl<ServerType>::setSslConfiguration(const SslConfiguration &configuration)
{
    this->_configuration = configuration;
}


template<typename ServerType>
SslConfiguration WithSsl<ServerType>::sslConfiguration() const
{
    return this->_configuration;
}



template<typename ServerType>
void WithSsl<ServerType>::setSslHandshakeTimeout(float sslHandshakeTimeout)
{
    this->_sslHandshakeTimeout = sslHandshakeTimeout;
}


template<typename ServerType>
float WithSsl<ServerType>::sslHandshakeTimeout() const
{
    return this->_sslHandshakeTimeout;
}


template<typename ServerType>
bool WithSsl<ServerType>::isSecure() const
{
    return true;
}


template<typename ServerType>
QSharedPointer<SocketLike> WithSsl<ServerType>::prepareRequest(QSharedPointer<SocketLike> request)
{
    try {
        Timeout timeout(_sslHandshakeTimeout);
        QSharedPointer<SslSocket> s = QSharedPointer<SslSocket>::create(request, _configuration);
        if (s->handshake(true)) {
            return asSocketLike(s);
        }
    }  catch (TimeoutException &) {
        //
    }
    return QSharedPointer<SocketLike>();
}


template<typename RequestHandler>
class SslServer: public WithSsl<TcpServer<RequestHandler>>
{
public:
    SslServer(const QHostAddress &serverAddress, quint16 serverPort);
    SslServer(const QHostAddress &serverAddress, quint16 serverPort, const SslConfiguration &configuration);
    SslServer(quint16 serverPort);
    SslServer(quint16 serverPort, const SslConfiguration &configuration);
};


template<typename RequestHandler>
SslServer<RequestHandler>::SslServer(const QHostAddress &serverAddress, quint16 serverPort)
    : WithSsl<TcpServer<RequestHandler>>(serverAddress, serverPort) {}


template<typename RequestHandler>
SslServer<RequestHandler>::SslServer(const QHostAddress &serverAddress, quint16 serverPort, const SslConfiguration &configuration)
    : WithSsl<TcpServer<RequestHandler>>(serverAddress, serverPort, configuration) {}


template<typename RequestHandler>
SslServer<RequestHandler>::SslServer(quint16 serverPort)
    : WithSsl<TcpServer<RequestHandler>>(serverPort) {}


template<typename RequestHandler>
SslServer<RequestHandler>::SslServer(quint16 serverPort, const SslConfiguration &configuration)
    : WithSsl<TcpServer<RequestHandler>>(serverPort, configuration) {}


#endif


class BaseRequestHandler
{
public:
    BaseRequestHandler();
    virtual ~BaseRequestHandler();
public:
    void run();
protected:
    virtual bool setup();
    virtual void handle();
    virtual void finish();
    template<typename UserDataType> UserDataType *userData();
public:
    QSharedPointer<SocketLike> request;
    BaseStreamServer *server;
};


template<typename UserDataType>
UserDataType *BaseRequestHandler::userData()
{
    return static_cast<UserDataType*>(server->userData());
}


class Socks5RequestHandlerPrivate;
class Socks5RequestHandler: public qtng::BaseRequestHandler
{
public:
    Socks5RequestHandler();
    virtual ~Socks5RequestHandler() override;
protected:
    virtual void doConnect(const QString &hostName, const QHostAddress &hostAddress, quint16 port);
    bool sendConnectReply(const QHostAddress &hostAddress, quint16 port);
    virtual void doFailed(const QString &hostName, const QHostAddress &hostAddress, quint16 port);
    bool sendFailedReply();
    virtual void log(const QString &hostName, const QHostAddress &hostAddress, quint16 port, bool success);
protected:
    virtual void handle() override;
private:
    Socks5RequestHandlerPrivate * const d_ptr;
    Q_DECLARE_PRIVATE(Socks5RequestHandler)
};

QTNETWORKNG_NAMESPACE_END

#endif

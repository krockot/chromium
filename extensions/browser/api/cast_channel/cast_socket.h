// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_BROWSER_API_CAST_CHANNEL_CAST_SOCKET_H_
#define EXTENSIONS_BROWSER_API_CAST_CHANNEL_CAST_SOCKET_H_

#include <queue>
#include <string>

#include "base/basictypes.h"
#include "base/cancelable_callback.h"
#include "base/gtest_prod_util.h"
#include "base/memory/ref_counted.h"
#include "base/threading/thread_checker.h"
#include "base/timer/timer.h"
#include "extensions/browser/api/api_resource.h"
#include "extensions/browser/api/api_resource_manager.h"
#include "extensions/browser/api/cast_channel/cast_socket.h"
#include "extensions/browser/api/cast_channel/cast_transport.h"
#include "extensions/common/api/cast_channel.h"
#include "extensions/common/api/cast_channel/logging.pb.h"
#include "net/base/completion_callback.h"
#include "net/base/io_buffer.h"
#include "net/base/ip_endpoint.h"
#include "net/base/net_log.h"

namespace net {
class AddressList;
class CertVerifier;
class SSLClientSocket;
class StreamSocket;
class TCPClientSocket;
class TransportSecurityState;
}

namespace extensions {
namespace core_api {
namespace cast_channel {
class CastMessage;
class Logger;
struct LastErrors;
class MessageFramer;

// Cast device capabilities.
enum CastDeviceCapability {
  NONE = 0,
  VIDEO_OUT = 1 << 0,
  VIDEO_IN = 1 << 1,
  AUDIO_OUT = 1 << 2,
  AUDIO_IN = 1 << 3,
  DEV_MODE = 1 << 4
};

// Public interface of the CastSocket class.
class CastSocket : public ApiResource {
 public:
  explicit CastSocket(const std::string& owner_extension_id);
  ~CastSocket() override {}

  // Used by BrowserContextKeyedAPIFactory.
  static const char* service_name() { return "CastSocketImplManager"; }

  // Connects the channel to the peer. If successful, the channel will be in
  // READY_STATE_OPEN.  DO NOT delete the CastSocket object in |callback|.
  // Instead use Close().
  // |callback| will be invoked with any ChannelError that occurred, or
  // CHANNEL_ERROR_NONE if successful.
  // |delegate| receives message receipt and error events.
  // Ownership of |delegate| is transferred to this CastSocket.
  virtual void Connect(scoped_ptr<CastTransport::Delegate> delegate,
                       base::Callback<void(ChannelError)> callback) = 0;

  // Closes the channel if not already closed. On completion, the channel will
  // be in READY_STATE_CLOSED.
  //
  // It is fine to delete this object in |callback|.
  virtual void Close(const net::CompletionCallback& callback) = 0;

  // The IP endpoint for the destination of the channel.
  virtual const net::IPEndPoint& ip_endpoint() const = 0;

  // Channel id generated by the ApiResourceManager.
  virtual int id() const = 0;

  // Sets the channel id generated by ApiResourceManager.
  virtual void set_id(int id) = 0;

  // The authentication level requested for the channel.
  virtual ChannelAuthType channel_auth() const = 0;

  // Returns a cast:// or casts:// URL for the channel endpoint.
  // For backwards compatibility.
  virtual std::string cast_url() const = 0;

  // The ready state of the channel.
  virtual ReadyState ready_state() const = 0;

  // Returns the last error that occurred on this channel, or
  // CHANNEL_ERROR_NONE if no error has occurred.
  virtual ChannelError error_state() const = 0;

  // True when keep-alive signaling is handled for this socket.
  virtual bool keep_alive() const = 0;

  // Marks a socket as invalid due to an error. Errors close the socket
  // and any further socket operations will return the error code
  // net::SOCKET_NOT_CORRECTED.
  // Setting the error state does not close the socket if it is open.
  virtual void SetErrorState(ChannelError error_state) = 0;

  // Returns a pointer to the socket's message transport layer. Can be used to
  // send and receive CastMessages over the socket.
  virtual CastTransport* transport() const = 0;

  // Tells the ApiResourceManager to retain CastSocket objects even
  // if their corresponding extension is suspended.
  // (CastSockets are still deleted if the extension is removed entirely from
  // the browser.)
  bool IsPersistent() const override;
};

// This class implements a channel between Chrome and a Cast device using a TCP
// socket with SSL.  The channel may authenticate that the receiver is a genuine
// Cast device.  All CastSocketImpl objects must be used only on the IO thread.
//
// NOTE: Not called "CastChannel" to reduce confusion with the generated API
// code.
class CastSocketImpl : public CastSocket {
 public:
  // Creates a new CastSocket that connects to |ip_endpoint| with
  // |channel_auth|. |owner_extension_id| is the id of the extension that opened
  // the socket.  |channel_auth| must not be CHANNEL_AUTH_NONE.
  // Parameters:
  // |owner_extension_id|: ID of the extension calling the API.
  // |ip_endpoint|: IP address of the remote host.
  // |channel_auth|: Authentication method used for connecting to a Cast
  //                 receiver.
  // |net_log|: Log of socket events.
  // |connect_timeout|: Connection timeout interval.
  // |logger|: Log of cast channel events.
  CastSocketImpl(const std::string& owner_extension_id,
                 const net::IPEndPoint& ip_endpoint,
                 ChannelAuthType channel_auth,
                 net::NetLog* net_log,
                 const base::TimeDelta& connect_timeout,
                 bool keep_alive,
                 const scoped_refptr<Logger>& logger,
                 long device_capabilities);

  // Ensures that the socket is closed.
  ~CastSocketImpl() override;

  // CastSocket interface.
  void Connect(scoped_ptr<CastTransport::Delegate> delegate,
               base::Callback<void(ChannelError)> callback) override;
  CastTransport* transport() const override;
  void Close(const net::CompletionCallback& callback) override;
  const net::IPEndPoint& ip_endpoint() const override;
  int id() const override;
  void set_id(int channel_id) override;
  ChannelAuthType channel_auth() const override;
  std::string cast_url() const override;
  ReadyState ready_state() const override;
  ChannelError error_state() const override;
  bool keep_alive() const override;

  // Required by ApiResourceManager.
  static const char* service_name() { return "CastSocketManager"; }

 protected:
  // CastTransport::Delegate methods for receiving handshake messages.
  class AuthTransportDelegate : public CastTransport::Delegate {
   public:
    AuthTransportDelegate(CastSocketImpl* socket);

    // CastTransport::Delegate interface.
    void OnError(ChannelError error_state,
                 const LastErrors& last_errors) override;
    void OnMessage(const CastMessage& message) override;
    void Start() override;

   private:
    CastSocketImpl* socket_;
  };

  // Replaces the internally-constructed transport object with one provided
  // by the caller (e.g. a mock).
  void SetTransportForTesting(scoped_ptr<CastTransport> transport);

  // Verifies whether the socket complies with cast channel policy.
  // Audio only channel policy mandates that a device declaring a video out
  // capability must not have a certificate with audio only policy.
  bool VerifyChannelPolicy(const AuthResult& result);

  // Delegate for receiving handshake messages/errors.
  AuthTransportDelegate auth_delegate_;

 private:
  FRIEND_TEST_ALL_PREFIXES(CastSocketTest, TestConnectAuthMessageCorrupted);
  FRIEND_TEST_ALL_PREFIXES(CastSocketTest,
                           TestConnectChallengeReplyReceiveError);
  FRIEND_TEST_ALL_PREFIXES(CastSocketTest,
                           TestConnectChallengeVerificationFails);
  friend class AuthTransportDelegate;
  friend class ApiResourceManager<CastSocketImpl>;
  friend class CastSocketTest;
  friend class TestCastSocket;

  void SetErrorState(ChannelError error_state) override;

  // Frees resources and cancels pending callbacks.  |ready_state_| will be set
  // READY_STATE_CLOSED on completion.  A no-op if |ready_state_| is already
  // READY_STATE_CLOSED.
  void CloseInternal();

  // Creates an instance of TCPClientSocket.
  virtual scoped_ptr<net::TCPClientSocket> CreateTcpSocket();
  // Creates an instance of SSLClientSocket with the given underlying |socket|.
  virtual scoped_ptr<net::SSLClientSocket> CreateSslSocket(
      scoped_ptr<net::StreamSocket> socket);
  // Extracts peer certificate from SSLClientSocket instance when the socket
  // is in cert error state.
  // Returns whether certificate is successfully extracted.
  virtual bool ExtractPeerCert(std::string* cert);
  // Verifies whether the challenge reply received from the peer is valid:
  // 1. Signature in the reply is valid.
  // 2. Certificate is rooted to a trusted CA.
  virtual bool VerifyChallengeReply();

  // Invoked by a cancelable closure when connection setup time
  // exceeds the interval specified at |connect_timeout|.
  void OnConnectTimeout();

  /////////////////////////////////////////////////////////////////////////////
  // Following methods work together to implement the following flow:
  // 1. Create a new TCP socket and connect to it
  // 2. Create a new SSL socket and try connecting to it
  // 3. If connection fails due to invalid cert authority, then extract the
  //    peer certificate from the error.
  // 4. Whitelist the peer certificate and try #1 and #2 again.
  // 5. If SSL socket is connected successfully, and if protocol is casts://
  //    then issue an auth challenge request.
  // 6. Validate the auth challenge response.
  //
  // Main method that performs connection state transitions.
  void DoConnectLoop(int result);
  // Each of the below Do* method is executed in the corresponding
  // connection state. For example when connection state is TCP_CONNECT
  // DoTcpConnect is called, and so on.
  int DoTcpConnect();
  int DoTcpConnectComplete(int result);
  int DoSslConnect();
  int DoSslConnectComplete(int result);
  int DoAuthChallengeSend();
  int DoAuthChallengeSendComplete(int result);
  int DoAuthChallengeReplyComplete(int result);
  /////////////////////////////////////////////////////////////////////////////

  // Schedules asynchrous connection loop processing in the MessageLoop.
  void PostTaskToStartConnectLoop(int result);

  // Runs the external connection callback and resets it.
  void DoConnectCallback();

  virtual bool CalledOnValidThread() const;

  virtual base::Timer* GetTimer();

  void SetConnectState(proto::ConnectionState connect_state);
  void SetReadyState(ReadyState ready_state);

  base::ThreadChecker thread_checker_;

  const std::string owner_extension_id_;
  // The id of the channel.
  int channel_id_;
  // The IP endpoint that the the channel is connected to.
  net::IPEndPoint ip_endpoint_;
  // Receiver authentication requested for the channel.
  ChannelAuthType channel_auth_;
  // The NetLog for this service.
  net::NetLog* net_log_;
  // The NetLog source for this service.
  net::NetLog::Source net_log_source_;
  // True when keep-alive signaling should be handled for this socket.
  bool keep_alive_;

  // Shared logging object, used to log CastSocket events for diagnostics.
  scoped_refptr<Logger> logger_;

  // CertVerifier is owned by us but should be deleted AFTER SSLClientSocket
  // since in some cases the destructor of SSLClientSocket may call a method
  // to cancel a cert verification request.
  scoped_ptr<net::CertVerifier> cert_verifier_;
  scoped_ptr<net::TransportSecurityState> transport_security_state_;

  // Owned ptr to the underlying TCP socket.
  scoped_ptr<net::TCPClientSocket> tcp_socket_;

  // Owned ptr to the underlying SSL socket.
  scoped_ptr<net::SSLClientSocket> socket_;

  // Certificate of the peer. This field may be empty if the peer
  // certificate is not yet fetched.
  std::string peer_cert_;

  // Reply received from the receiver to a challenge request.
  scoped_ptr<CastMessage> challenge_reply_;

  // Callback invoked when the socket is connected or fails to connect.
  base::Callback<void(ChannelError)> connect_callback_;

  // Callback invoked by |connect_timeout_timer_| to cancel the connection.
  base::CancelableClosure connect_timeout_callback_;

  // Duration to wait before timing out.
  base::TimeDelta connect_timeout_;

  // Timer invoked when the connection has timed out.
  scoped_ptr<base::Timer> connect_timeout_timer_;

  // Set when a timeout is triggered and the connection process has
  // canceled.
  bool is_canceled_;

  // Capabilities declared by the cast device.
  long device_capabilities_;

  // Connection flow state machine state.
  proto::ConnectionState connect_state_;

  // Write flow state machine state.
  proto::WriteState write_state_;

  // Read flow state machine state.
  proto::ReadState read_state_;

  // The last error encountered by the channel.
  ChannelError error_state_;

  // The current status of the channel.
  ReadyState ready_state_;

  // Task invoked to (re)start the connect loop.  Canceled on entry to the
  // connect loop.
  base::CancelableClosure connect_loop_callback_;

  // Task invoked to send the auth challenge.  Canceled when the auth challenge
  // has been sent.
  base::CancelableClosure send_auth_challenge_callback_;

  // Cast message formatting and parsing layer.
  scoped_ptr<CastTransport> transport_;

  // Caller's message read and error handling delegate.
  scoped_ptr<CastTransport::Delegate> read_delegate_;

  DISALLOW_COPY_AND_ASSIGN(CastSocketImpl);
};
}  // namespace cast_channel
}  // namespace core_api
}  // namespace extensions

#endif  // EXTENSIONS_BROWSER_API_CAST_CHANNEL_CAST_SOCKET_H_

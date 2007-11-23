#include "TcpConnection.hxx"
#include <vector>
#include <boost/bind.hpp>
#include <rutil/SharedPtr.hxx>
#include "ConnectionManager.hxx"
#include "RequestHandler.hxx"

using namespace std;
using namespace resip;

namespace reTurn {

TcpConnection::TcpConnection(asio::io_service& ioService,
    ConnectionManager& manager, RequestHandler& handler, bool turnFraming)
  : AsyncTcpSocketBase(ioService),
    mConnectionManager(manager),
    mRequestHandler(handler),
    mTurnFraming(turnFraming)
{
   registerAsyncSocketBaseHandler(this);
}

TcpConnection::~TcpConnection()
{
   std::cout << "TcpConnection destroyed." << std::endl;
   registerAsyncSocketBaseHandler(0);
}

asio::ip::tcp::socket& 
TcpConnection::socket()
{
  return mSocket;
}

void 
TcpConnection::start()
{
   std::cout << "TcpConnection started." << std::endl;
   doFramedReceive();
}

void 
TcpConnection::stop()
{
  mSocket.close();
}

void 
TcpConnection::close()
{
   mConnectionManager.stop(shared_from_this());
}

void 
TcpConnection::onReceiveSuccess(unsigned int socketDesc, const asio::ip::address& address, unsigned short port, resip::SharedPtr<resip::Data> data)
{
   if (data->size() > 4)
   {
      char* stunMessageBuffer = 0;
      unsigned int stunMessageSize = 0;

      bool treatAsData=false;
      /*
      std::cout << "Read " << bytesTransferred << " bytes from tcp socket (" << address.to_string() << ":" << port << "): " << std::endl;
      cout << std::hex;
      for(int i = 0; i < data->size(); i++)
      {
         std::cout << (char)(*data)[i] << "(" << int((*data)[i]) << ") ";
      }
      std::cout << std::dec << std::endl;
      */
      unsigned short channelNumber;
      memcpy(&channelNumber, &(*data)[0], 2);
      channelNumber = ntohs(channelNumber);

      if(mTurnFraming)
      {
         // All Turn messaging will be framed
         if(channelNumber == 0) // Stun/Turn Request
         {
            stunMessageBuffer = (char*)&(*data)[4];
            stunMessageSize = (unsigned int)data->size()-4;
         }
         else  
         {
            // Turn Data
            treatAsData = true;
         }
      }
      else
      {
         stunMessageBuffer = (char*)&(*data)[0];
         stunMessageSize = data->size();
      }

      if(!treatAsData)
      {
         if(stunMessageBuffer && stunMessageSize)
         {
            // Try to parse stun message
            StunMessage request(StunTuple(StunTuple::TCP, mSocket.local_endpoint().address(), mSocket.local_endpoint().port()),
                                StunTuple(StunTuple::TCP, address, port),
                                stunMessageBuffer, stunMessageSize);
            if(request.isValid())
            {
               StunMessage response;
               RequestHandler::ProcessResult result = mRequestHandler.processStunMessage(this, request, response);

               switch(result)
               {
               case RequestHandler::NoResponseToSend:
                  // No response to send - just receive next message
                  doFramedReceive();
                  return;
               case RequestHandler::RespondFromAlternatePort:
               case RequestHandler::RespondFromAlternateIp:
               case RequestHandler::RespondFromAlternateIpPort:
                  // These only happen for UDP server for RFC3489 backwards compatibility
                  assert(false);
                  break;
               case RequestHandler::RespondFromReceiving:
               default:
                  break;
               }

#define RESPONSE_BUFFER_SIZE 1024
               SharedPtr<Data> buffer = allocateBuffer(RESPONSE_BUFFER_SIZE);
               unsigned int responseSize;
               if(mTurnFraming)  
               {
                  responseSize = response.stunEncodeFramedMessage((char*)buffer->data(), RESPONSE_BUFFER_SIZE);
               }
               else
               {
                  responseSize = response.stunEncodeMessage((char*)buffer->data(), RESPONSE_BUFFER_SIZE);
               }
               buffer->truncate(responseSize);  // set size to real size

               doSend(response.mRemoteTuple, buffer);
            }
         }
         else
         {
            close();
            return;
         }
      } 
      else
      {
         mRequestHandler.processTurnData(channelNumber,
                                         StunTuple(StunTuple::TCP, mSocket.local_endpoint().address(), mSocket.local_endpoint().port()),
                                         StunTuple(StunTuple::TCP, address, port),
                                         data);
      }
   }
   else
   {
      cout << "TcpConnection::onReceiveSuccess not enough data for framed message - discarding!" << endl;
      close();
      return;
   }

   doFramedReceive();
}

void 
TcpConnection::onReceiveFailure(unsigned int socketDesc, const asio::error_code& e)
{
   if(e != asio::error::operation_aborted)
   {
      cout << "TcpConnection::onReceiveFailure: " << e.message() << endl;

      close();
   }
}

void
TcpConnection::onSendSuccess(unsigned int socketDesc)
{
}

void
TcpConnection::onSendFailure(unsigned int socketDesc, const asio::error_code& error)
{
   if(error != asio::error::operation_aborted)
   {
      cout << "TcpConnection::onSendFailure: " << error.message() << endl;
      close();
   }
}

} 


/* ====================================================================

 Original contribution Copyright (C) 2007 Plantronics, Inc.
 Provided under the terms of the Vovida Software License, Version 2.0.

 The Vovida Software License, Version 2.0 
 
 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions
 are met:
 
 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in
    the documentation and/or other materials provided with the
    distribution. 
 
 THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESSED OR IMPLIED
 WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, TITLE AND
 NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT
 OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT DAMAGES
 IN EXCESS OF $1,000, NOR FOR ANY INDIRECT, INCIDENTAL, SPECIAL,
 EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 DAMAGE.

 ==================================================================== */


# Simple web server
A simple web server to handle GET and HEAD request

#### HTTP

The Hypertext Transfer Protocol (HTTP) is an application-level
   protocol for distributed, collaborative, hypermedia information
   systems. It is a generic, stateless, protocol which can be used for
   many tasks beyond its use for hypertext, such as name servers and
   distributed object management systems, through extension of its
   request methods, error codes and headers. A feature of HTTP is
   the typing and negotiation of data representation, allowing systems
   to be built independently of the data being transferred.

   HTTP has been in use by the World-Wide Web global information
   initiative since 1990. This specification defines the protocol
   referred to as "HTTP/1.1", and is an update to RFC 2068
   

#### GET

   The GET method means retrieve whatever information (in the form of an
   entity) is identified by the Request-URI. If the Request-URI refers
   to a data-producing process, it is the produced data which shall be
   returned as the entity in the response and not the source text of the
   process, unless that text happens to be the output of the process.

#### POST

The HEAD method is identical to GET except that the server MUST NOT
   return a message-body in the response. The metainformation contained
   in the HTTP headers in response to a HEAD request SHOULD be identical
   to the information sent in response to a GET request. This method can
   be used for obtaining metainformation about the entity implied by the
   request without transferring the entity-body itself. This method is
   often used for testing hypertext links for validity, accessibility,
   and recent modification.

Ref: https://tools.ietf.org/html/rfc2616#section-9.3

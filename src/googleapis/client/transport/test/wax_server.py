#!/usr/bin/python
# Copyright 2013 Google Inc. All Rights Reserved.
"""Implements the Wax Service testing API as a simple web service.

This is a basic implementation of the Wax Service in a form that lends
itself to testing in an open source release since the Wax Service is not
yet publically accessible. It is implemented as a plain web server rather
than Google Cloud Service, but the HTTP interface is the same. Therefore,
this is suitable for testing basic HTTP interactions and end-to-end testing
for many client features.

Usage:
  wax_server [-g] [--port=<port>] [--signal_pid=<pid>]
     -g allows the server to accept connections from any IP.
     The default is only from localhost.

     --port specifies the port to listen on.
     The default is 5000.

     --signal_pid specifies a PID that should be notified with SIGUSR1 when
       the server is read.
"""

from BaseHTTPServer import BaseHTTPRequestHandler
from BaseHTTPServer import HTTPServer

import copy
import getopt
import json
import os
import re
import signal
import string
import sys
import threading
import time


_RE_SESSION_COMMAND = re.compile('^/sessions/([^/]+)/items')
_RE_ITEM_COMMAND = re.compile('^/sessions/([^/]+)/items/([^/]+)')
_JSON_CONTENT_TYPE = 'application/json'
_SERVER_SETTINGS = {'run': True}


class SessionData(object):
  """Session data maintains a list of objects for a given session id.

  These objects are thread-safe and live over the lifetime of the server.
  They are destroyed by explicitly removing the session, which is not at
  all reliable since clients are not required to do so, and may crash, etc.
  However, this is a testing server that is not expected to be run for long
  periods of time.

  A session contains a list of items where an item is a dictionary.
  Items have a 'kind' attribute to make them compatible with the Wax Service.
  The session is initialized with two items, A and B, to be compatible with
  the Wax Service.
  """

  def __init__(self):
    """Constructs a new session instance."""
    self._created = time.clock()
    self._mutex = threading.Lock()
    self._items = []

    # Add items A and B by default to mimick the service's behavior.
    self._items.append(self.AddWaxKind({'id': 'A', 'name': 'Item A'}))
    self._items.append(self.AddWaxKind({'id': 'B', 'name': 'Item B'}))

  def _FindItem(self, key):
    """Finds item with the given id, if present in the session.

    This method is not thread-safe. It relies on the caller to lock.

    Args:
      key: (string) Identifier to search for.

    Returns:
      A reference to the item or None if one is not present.
    """

    for session_item in self._items:
      if session_item['id'] == key:
        return session_item
    return None

  def AddNewItem(self, key, item):
    """Add an item to the session list if the specified identifier is unique.

    Args:
      key: (string) The item's identifier
      item: (dict) The item to add must have an 'id'' and will be given
                   a 'kind' attribute.

    Returns:
      The actual values for the added item (including added values).
      None indicates a failure (because the identifier already exists)
    """
    wax = None
    self._mutex.acquire()
    if not self._FindItem(key):
      wax = self.AddWaxKind(item)
      self._items.append(copy.deepcopy(wax))
    self._mutex.release()
    return wax

  def ReplaceItem(self, key, item):
    """Inserts the item into the session, or replaces the item for identifer.

    Args:
      key: (string) The items identifier.
      item: (dict) The item to insert or replace the existing item with.
                   A 'kind' attribute will be added if needed.
    Returns:
       A copy of the inserted item.
    """
    wax = self.AddWaxKind(item)
    self._mutex.acquire()
    session_item = self._FindItem(key)
    if session_item:
      self._items.remove(session_item)

    self._items.append(copy.deepcopy(wax))
    self._mutex.release()
    return wax

  def PatchItem(self, key, item):
    """Updates an existing item with the given id with the elements of item.

    This overwrites the existing data with the new data. Any old values that
    were not contained in item are preserved.

    Args:
      key: (string) The identifier to update
      item: (dict) The particular values to update.

    Returns:
      A copy of the patched item or None on falure.
    """
    wax = None
    self._mutex.acquire()
    session_item = self._FindItem(key)
    if session_item:
      self._items.remove(session_item)
      wax = dict(session_item.items() + item.items())
      self._items.append(copy.deepcopy(wax))

    self._mutex.release()
    return wax

  def DeleteItem(self, key):
    """Deletes an item from the session.

    Args:
      key: (string) The identifier of the item to delete.

    Returns:
      The deleted item or None.
    """
    self._mutex.acquire()
    session_item = self._FindItem(key)
    if session_item:
      self._items.remove(session_item)
    self._mutex.release()
    return session_item

  def GetItemCopy(self, key):
    """Get a copy of the item with the given identifier.

    This returns a copy for simple thread-safety.

    Args:
      key: (string) The identifier of the item to return

    Returns:
      None if the identifier isnt present. Otherwise a copy of the item.
    """
    result = None
    self._mutex.acquire()
    session_item = self._FindItem(key)
    if session_item:
      result = copy.deepcopy(session_item)
    self._mutex.release()
    return result

  def GetAllItemsCopy(self):
    """Returns a copy of the list of all items.

    Returns:
      A copy is returned for simple thread-safety.
    """
    self._mutex.acquire()
    result = copy.deepcopy(self._items)
    self._mutex.release()
    return result

  def AddWaxKind(self, item):
    """Helper function that adds the 'kind' attribute to an item.

    Args:
      item: (dict)  The item to modify.

    Returns:
      A reference to the item with a 'kind' attribute added.
    """
    item['kind'] = 'wax#waxDataItem'
    return item


class Repository(object):
  """A repository of sessions keyed by sessionId.

     The repository is thread-safe.
  """

  def __init__(self):
    """Initializes an empty repository."""
    self._sessions = {}
    self._nodeid = str(time.clock())
    self._sequence_num = 0
    self._mutex = threading.Lock()

  def GetSessionData(self, key):
    """Returns a reference to the session with the given identifier.

    Args:
      key: (string) The session identifier.

    Returns:
      A reference to the SessionData or None.
    """
    self._mutex.acquire()
    session_data = self._sessions.get(key)
    self._mutex.release()
    return session_data

  def RemoveIdentifier(self, key):
    """Removes a session identifier and its data.

    Args:
      key: (string) The session identifier.

    Returns:
      The session data removed or None
    """
    self._mutex.acquire()
    session_data = self._sessions.pop(key, None)
    self._mutex.release()

    return session_data

  def NewIdentifier(self, basename):
    """Adds a new empty session and gives it a new unique identifier.

    Args:
       basename: (string) The prefix for the generated identifier.
    Returns:
       The generated identifier to refer to the SessionData in the future.
    """
    self._mutex.acquire()
    self._sequence_num += 1
    key = '%s-%s-%s' % (basename, self._nodeid, self._sequence_num)
    self._sessions[key] = SessionData()
    self._mutex.release()
    return key


REPOSITORY_ = Repository()


class WaxHandler(BaseHTTPRequestHandler):
  """Implements a Wax Service interface for the web server."""

  def _SendResponse(self, payload, http_code, content_type='text/plain'):
    """Send HTTP response.

    Args:
      payload: (string) The HTTP response body.
      http_code: (int) The HTTP response status code.
      content_type: (string) The HTTP Content-Type header.

    Returns:
      The http_code.
    """
    self.send_response(http_code)
    self.send_header('Content-Type', content_type)
    self.end_headers()
    self.wfile.write(payload)
    return http_code

  def _SendJsonObjectResponse(self, obj, http_code):
    """Sends object as a JSON-encoded HTTP response.

    Args:
      obj: (dict) The object to return
      http_code: (int) The HTTP response status code.

    Returns:
      The http_code.
    """
    return self._SendResponse(
        json.dumps(obj), http_code, content_type=_JSON_CONTENT_TYPE)

  def _SendJsonErrorResponse(self, msg, http_code):
    """Sends a JSON a JSON-encoded HTTP response containing the message.

    Args:
      msg: (sting) The message to send (explaining error).
      http_code: (int) The HTTP response status code.

    Returns:
      The http_code.
    """
    return self._SendResponse(
        json.dumps({'message': msg, 'code': http_code}),
        http_code,
        content_type=_JSON_CONTENT_TYPE)

  def _ProcessNewSessionCommand(self):
    """Adds a new session and sends a response.

    Responds with JSON containing a newSessionId attribute.

    Returns:
      The final HTTP status code.
    """
    request_json = self._GetJson()
    if not request_json:
      return self._SendJsonErrorResponse('Invalid JSON', 400)

    key = REPOSITORY_.NewIdentifier(request_json['sessionName'])
    return self._SendJsonObjectResponse(
        {'kind': 'wax#waxNewSession', 'newSessionId': key}, 200)

  def _ProcessRemoveSessionCommand(self):
    """Removes the session and sends a response.

    Responds with JSON containing a removeSessionId attribute.

    Returns:
      The final HTTP status code.
    """
    request_json = self._GetJson()
    if not request_json:
      return self._SendJsonErrorResponse('Invalid JSON', 400)

    key = request_json['sessionId']
    old_item = REPOSITORY_.RemoveIdentifier(key)
    if not old_item:
      return self._SendJsonErrorResponse('Unknown sessionId', 404)

    return self._SendJsonObjectResponse(
        {'kind': 'wax#waxRemoveSession', 'removeSessionId': key}, 200)

  def _ProcessGetSessionItemsCommand(self, session_data):
    """Sends a JSON response with the session items.

    Args:
      session_data: (SessionData) The SessionData instance

    Returns:
      The final HTTP status code.
    """
    items_copy = session_data.GetAllItemsCopy()
    return self._SendJsonObjectResponse(
        {'kind': 'wax#waxList', 'items': items_copy}, 200)

  def _ProcessInsertNewItemCommand(self, session_data):
    """Adds the item specified by the JSON payload and send a JSON response.

    Args:
      session_data: (SessionData) The SessionData instance

    Returns:
      The final HTTP status code.
    """
    request_json = self._GetJson()

    # TODO(user): 20130624
    # Distinguish {} from no payload.
    if not request_json:
      return self._SendJsonErrorResponse('Invalid JSON', 400)

    item_id = request_json.get('id', None)
    if not item_id:
      return self._SendJsonErrorResponse('JSON missing id', 400)

    added = session_data.AddNewItem(item_id, request_json)
    if added:
      time.sleep(3.0 / 1000.0)  # delay 3 ms so we can test timeouts
      return self._SendJsonObjectResponse(added, 200)
    else:
      return self._SendJsonErrorResponse('Item already exists', 403)

  def _ProcessGetItemCommand(self, session_data, item_id):
    """Returns a JSON response with the specified item.

    Args:
      session_data: (SessionData) The SessionData instance
      item_id: (string) The item id to return

    Returns:
      The final HTTP status code.
    """
    item_copy = session_data.GetItemCopy(item_id)
    if item_copy:
      return self._SendJsonObjectResponse(item_copy, 200)
    return self._SendJsonErrorResponse('Unknown item', 404)

  def _ProcessDeleteItemCommand(self, session_data, item_id):
    """Deletes the specified item and sends a response.

    Args:
      session_data: (SessionData) The SessionData instance.
      item_id: (string) The item id to return.

    Returns:
      The final HTTP status code.
    """
    if session_data.DeleteItem(item_id):
      return self._SendResponse('', 204)
    else:
      return self._SendJsonErrorResponse('Unknown item in session', 404)

  def _ProcessPatchItemCommand(self, session_data, item_id, request_json):
    """Patches the specified item with the JSON payload and responds.

    Args:
      session_data: (SessionData) The SessionData instance.
      item_id: (string) The item id to return.
      request_json: (dict) The value used to patch

    Returns:
      The final HTTP status code.
    """
    patched_item = session_data.PatchItem(item_id, request_json)
    if patched_item:
      return self._SendJsonObjectResponse(patched_item, 200)
    else:
      return self._SendJsonErrorResponse('Unknown item in session', 404)

  def _ProcessUpdateItemCommand(self, session_data, item_id, request_json):
    """Replaces the specified item with the JSON payload and responds.

    Args:
      session_data: (SessionData) The SessionData instance.
      item_id: (string) The item id to return.
      request_json: (dict) The replacement value.

    Returns:
      The final HTTP status code.
    """
    request_json.setdefault('id', item_id)
    if request_json['id'] != item_id:
      return self._SendJsonErrorResponse('Mismatched item ids', 400)

    wax = session_data.ReplaceItem(item_id, request_json)
    return self._SendJsonObjectResponse(wax, 200)

  def _ReadChunkedPayload(self):
    """Reads a payload sent with a chunked Transfer-Encoding.

    Raises:
      ValueError: if the payload was not properly chunk encoded.

    Returns:
      The decoded payload string.
    """
    payload = []
    while True:
      chunk_len = 0
      while True:
        hex_digit = self.rfile.read(1)
        if hex_digit in string.hexdigits:
          chunk_len = 16 * chunk_len + int(hex_digit, 16)
        elif hex_digit != '\r' or self.rfile.read(1) != '\n':
          raise ValueError('Expected \\r\\n termination')
        else:
          break

      if chunk_len == 0:
        break
      payload.append(self.rfile.read(chunk_len))

    return ''.join(payload)

  def _GetJson(self):
    """Reads JSON object from payload.

    Returns:
      JSON decoded object
    """
    encoding = self.headers.getheader('Transfer-Encoding')
    if encoding == 'chunked':
      payload = self._ReadChunkedPayload()
    else:
      length = int(self.headers.getheader('Content-Length'))
      payload = self.rfile.read(length)
      if (not payload
          or self.headers.getheader('Content-Type') != _JSON_CONTENT_TYPE):
        return None

    json_item = json.loads(payload)
    if not json_item:
      return None

    return json_item

  def _DispatchMethod(self, method):
    """Executes WAX method and sends response.

    Args:
      method: (string) The HTTP method type received.

    Returns:
      The response HTTP status code sent.
    """
    if self.path == '/quit':
      _SERVER_SETTINGS['run'] = False
      return self._SendResponse('BYE', 200)

    if self.path == '/newsession' and method == 'POST':
      return self._ProcessNewSessionCommand()

    if self.path == '/removesession' and method == 'POST':
      return self._ProcessRemoveSessionCommand()

    match = _RE_ITEM_COMMAND.match(self.path)
    if match:
      self._HandleItemMethod(method, match.group(1), match.group(2))
      return

    match = _RE_SESSION_COMMAND.match(self.path)
    if match:
      self._HandleSessionMethod(method, match.group(1))
      return

    self._SendJsonErrorResponse('URL Not Available', 404)

  def _HandleSessionMethod(self, method, session_id):
    """Executes method on wax sessions resource and sends response.

    Args:
      method: (string) HTTP method type.
      session_id: (string) Wax session identifier.

    Returns:
      HTTP status code.
    """
    session_data = REPOSITORY_.GetSessionData(session_id)
    if not session_data:
      self._SendJsonErrorResponse('Unknown sessionId', 404)
      return

    if method == 'GET':
      return self._ProcessGetSessionItemsCommand(session_data)

    if method == 'POST':
      return self._ProcessInsertNewItemCommand(session_data)

    return self._SendJsonErrorResponse('Unhandled method', 405)

  def _HandleItemMethod(self, method, session_id, item_id):
    """Executes method on wax items resource and sends response.

    Args:
      method: (string) HTTP method type.
      session_id: (string) Wax session identifier.
      item_id: (string) Wax item identifier.

    Returns:
      HTTP status code.
    """
    session_data = REPOSITORY_.GetSessionData(session_id)
    if not session_data:
      return self._SendJsonErrorResponse('Unknown sessionId', 404)

    if method == 'GET':
      return self._ProcessGetItemCommand(session_data, item_id)

    if method == 'DELETE':
      return self._ProcessDeleteItemCommand(session_data, item_id)

    # TODO(user): 20130624
    # Distinguish {} from no payload.
    request_json = self._GetJson()
    if not request_json:
      return self._SendJsonErrorResponse('Invalid JSON', 400)

    if method == 'PATCH':
      return self._ProcessPatchItemCommand(session_data, item_id, request_json)

    if method == 'PUT':
      return self._ProcessUpdateItemCommand(session_data, item_id, request_json)

    return self._SendJsonErrorResponse('Unhandled method', 405)

  def do_GET(self):     # pylint: disable=g-bad-name
    self._DispatchMethod('GET')

  def do_DELETE(self):  # pylint: disable=g-bad-name
    self._DispatchMethod('DELETE')

  def do_PATCH(self):   # pylint: disable=g-bad-name
    self._DispatchMethod('PATCH')

  def do_POST(self):    # pylint: disable=g-bad-name
    self._DispatchMethod('POST')

  def do_PUT(self):     # pylint: disable=g-bad-name
    self._DispatchMethod('PUT')


def main(argv):
  """Runs the program."""
  try:
    opts = getopt.getopt(sys.argv[1:], '-g', ['port=', 'signal_pid='])[0]
  except getopt.GetoptError:
    print '%s: [-g] [--port=<port>] [--signal_pid=<pid>]' % argv[0]
    sys.exit(1)

  signal_pid = 0
  host = '127.0.0.0'
  port = 5000
  for key, value in opts:
    if key == '--port':
      port = int(value)
      print 'Running on port: %d' % port
    elif key == '-g':
      host = '0.0.0.0'
    elif key == '--signal_pid':
      signal_pid = int(value)

  if host != '0.0.0.0':
    print 'Only available on localhost (-g not provided)'

  server = HTTPServer((host, port), WaxHandler)
  print 'Started WaxServer on %s:%s' % (host, port)
  if signal_pid != 0:
    print 'Server ready -- signaling %d' % signal_pid
    os.kill(signal_pid, signal.SIGUSR1)

  while _SERVER_SETTINGS['run']:
    server.handle_request()


if __name__ == '__main__':
  main(sys.argv)

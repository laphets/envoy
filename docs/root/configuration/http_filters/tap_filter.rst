.. _config_http_filters_tap:

Tap
===

* :ref:`v2 API reference <envoy_api_msg_config.filter.http.tap.v2alpha.Tap>`
* This filter should be configured with the name *envoy.filters.http.tap*.

.. attention::

  The tap filter is experimental and is currently under active development. There is currently a
  very limited set of match conditions, output configuration, output sinks, etc. Capabilities will
  be expanded over time and the configuration structures are likely to change.

The HTTP tap filter is used to tap HTTP traffic. At a high level, the configuration is composed of
two pieces:

1. Match configuration: a list of conditions under which the filter will match an HTTP request
   and begin a tap session.
2. Output configuration: a list of output sinks that the filter will write the matched and tapped
   data to.

Each of these concepts will be covered in the following example configuration section.

Example configuration
---------------------

Example filter configuration:

.. code-block:: yaml

  name: envoy.filters.http.tap
  config:
    admin_config:
      config_id: test_config_id

The previous snippet configures the filter for control via the :http:post:`/tap` admin handler.
See the following section for more details.

.. _config_http_filters_tap_admin_handler:

Admin handler
-------------

When the HTTP filter specifies an :ref:`admin_config
<envoy_api_msg_config.filter.http.tap.v2alpha.AdminConfig>`, it is configured for admin control and
the :http:post:`/tap` admin handler will be installed. The admin handler can be used for live
tapping and debugging of HTTP traffic. It works as follows:

1. A POST request is used to provide a valid tap configuration. The POST request body can be either
   the JSON or YAML representation of the :ref:`TapConfig
   <envoy_api_msg_service.tap.v2alpha.TapConfig>` message.
2. If the POST request is accepted, Envoy will stream :ref:`HttpBufferedTrace
   <envoy_api_msg_data.tap.v2alpha.HttpBufferedTrace>` messages (serialized to JSON) until the admin
   request is terminated.

An example POST body:

.. code-block:: yaml

  config_id: test_config_id
  tap_config:
    match_configs:
      - match_id: foo_match_id
        http_match_config:
          request_match_config:
            headers:
              - name: foo
                exact_match: bar
          response_match_config:
            headers:
              - name: bar
                exact_match: baz
    output_config:
      sinks:
        - streaming_admin: {}

The configuration instructs the tap filter to match any HTTP requests in which a request header
``foo: bar`` is present AND a response header ``bar: baz`` is present. If both of these conditions
are met, the request will be tapped and streamed out the admin endpoint.

Statistics
----------

The tap filter outputs statistics in the *http.<stat_prefix>.tap.* namespace. The :ref:`stat prefix
<envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.stat_prefix>`
comes from the owning HTTP connection manager.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  rq_tapped, Counter, Total requests that matched and were tapped

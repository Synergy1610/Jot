"""Minimal disposable StatusNotifier watcher for process integration tests."""
import gi

gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib

XML = """
<node>
  <interface name="org.kde.StatusNotifierWatcher">
    <method name="RegisterStatusNotifierItem">
      <arg type="s" name="service" direction="in"/>
    </method>
  </interface>
</node>
"""
node = Gio.DBusNodeInfo.new_for_xml(XML)


def method_call(connection, sender, path, interface, method, parameters, invocation):
    if method == "RegisterStatusNotifierItem":
        print("item:" + parameters.unpack()[0], flush=True)
        invocation.return_value(GLib.Variant("()", ()))


def bus_acquired(connection, name):
    connection.register_object("/StatusNotifierWatcher", node.interfaces[0],
                               method_call, None, None)


def name_acquired(connection, name):
    print("ready", flush=True)


Gio.bus_own_name(Gio.BusType.SESSION, "org.kde.StatusNotifierWatcher",
                 Gio.BusNameOwnerFlags.NONE, bus_acquired, name_acquired, None)
GLib.MainLoop().run()

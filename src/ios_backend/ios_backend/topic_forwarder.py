"""Generic ROS2 topic discovery, subscription, and serialization for the State Inspector.

Runs alongside the existing hand-coded callbacks in ios_backend_node.py.
Values are forwarded as raw SI units — no conversion, no field renaming.
"""

import threading
import time

import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile, DurabilityPolicy, ReliabilityPolicy


def _deep_convert(obj):
    """Recursively convert numpy types and OrderedDicts to JSON-safe Python."""
    if isinstance(obj, dict):
        return {k: _deep_convert(v) for k, v in obj.items()}
    if isinstance(obj, (list, tuple)):
        return [_deep_convert(v) for v in obj]
    if hasattr(obj, 'item'):     # numpy scalar
        return obj.item()
    if hasattr(obj, 'tolist'):   # numpy array
        return obj.tolist()
    return obj


def _build_known_types():
    """Build registry of message type string → Python class."""
    registry = {}

    # sim_msgs — all message types
    try:
        import sim_msgs.msg as sm
        for name in dir(sm):
            cls = getattr(sm, name)
            if isinstance(cls, type) and hasattr(cls, 'SLOT_TYPES'):
                registry[f'sim_msgs/msg/{name}'] = cls
    except ImportError:
        pass

    # std_msgs basics
    try:
        from std_msgs.msg import String, Bool, Float32, Float64
        registry['std_msgs/msg/String'] = String
        registry['std_msgs/msg/Bool'] = Bool
        registry['std_msgs/msg/Float32'] = Float32
        registry['std_msgs/msg/Float64'] = Float64
    except ImportError:
        pass

    # rosgraph_msgs
    try:
        from rosgraph_msgs.msg import Clock
        registry['rosgraph_msgs/msg/Clock'] = Clock
    except ImportError:
        pass

    # lifecycle_msgs
    try:
        from lifecycle_msgs.msg import TransitionEvent
        registry['lifecycle_msgs/msg/TransitionEvent'] = TransitionEvent
    except ImportError:
        pass

    return registry


# Topics published by ios_backend — exclude to avoid echo loops
_OWN_TOPICS = frozenset((
    '/aircraft/devices/instructor/failure_command',
    '/aircraft/devices/instructor/panel',
    '/aircraft/devices/virtual/panel',
    '/aircraft/devices/instructor/controls/avionics',
    '/aircraft/devices/instructor/controls/flight',
    '/aircraft/devices/instructor/controls/engine',
    '/sim/command',
    '/sim/diagnostics/heartbeat',
    '/sim/diagnostics/lifecycle',
))

# Skip internal ROS2 infrastructure topics
_SKIP_PREFIXES = (
    '/rosout',
    '/parameter_events',
    '/robot_description',
)


class TopicForwarder:
    """Dynamic topic discovery, subscription, and serialization."""

    def __init__(self, node: Node):
        self._node = node
        self._subscriptions = {}      # topic_path → subscription
        self._latest = {}             # topic_path → dict (serialized msg)
        self._topic_meta = {}         # topic_path → {type, has_data}
        self._lock = threading.Lock()
        self._last_forward_ts = {}    # topic_path → float (throttle)
        self._known_types = _build_known_types()
        self._node.get_logger().info(
            f'[TopicForwarder] {len(self._known_types)} known message types registered')

    def discover(self):
        """Called every 3s. Discovers new topics, creates subscriptions, cleans stale."""
        try:
            topic_list = self._node.get_topic_names_and_types()
        except Exception:
            return

        current_topics = set()
        for topic_path, type_names in topic_list:
            current_topics.add(topic_path)

            if topic_path in self._subscriptions:
                continue

            # Skip own publishers and infrastructure
            if topic_path in _OWN_TOPICS:
                continue
            if any(topic_path.startswith(p) for p in _SKIP_PREFIXES):
                continue

            # Find matching type in registry
            for type_name in type_names:
                if type_name in self._known_types:
                    msg_class = self._known_types[type_name]
                    self._create_subscription(topic_path, msg_class, type_name)
                    break

        # Cleanup stale subscriptions
        stale = [p for p in self._subscriptions if p not in current_topics]
        for p in stale:
            self._node.destroy_subscription(self._subscriptions[p])
            del self._subscriptions[p]
            with self._lock:
                self._latest.pop(p, None)
                self._topic_meta.pop(p, None)

    def _create_subscription(self, topic_path, msg_class, type_name):
        """Create a generic subscription with throttled callback."""

        def callback(msg, _path=topic_path):
            now = time.monotonic()
            last = self._last_forward_ts.get(_path, 0.0)
            if now - last < 0.2:  # 5 Hz max per topic
                return
            self._last_forward_ts[_path] = now
            try:
                from rosidl_runtime_py import message_to_ordereddict
                data = _deep_convert(message_to_ordereddict(msg))
                with self._lock:
                    self._latest[_path] = data
            except Exception:
                pass  # Don't crash on serialization failure

        # Transient local for capability-style topics
        if 'capabilities' in topic_path:
            qos = QoSProfile(
                depth=1,
                durability=DurabilityPolicy.TRANSIENT_LOCAL,
                reliability=ReliabilityPolicy.RELIABLE,
            )
        else:
            qos = 10

        sub = self._node.create_subscription(msg_class, topic_path, callback, qos)
        self._subscriptions[topic_path] = sub
        self._topic_meta[topic_path] = {'type': type_name, 'has_data': False}

    def get_topic_tree(self):
        """Returns topic tree metadata for WS topic_tree message."""
        with self._lock:
            tree = {}
            for path, meta in self._topic_meta.items():
                tree[path] = {
                    'type': meta['type'],
                    'has_data': path in self._latest,
                }
            return tree

    def get_all_values(self):
        """Returns all latest values for WS topic_update message."""
        with self._lock:
            return dict(self._latest)

import rclpy
from rclpy.node import Node
from rclpy.parameter import Parameter


class QtgEngineNode(Node):
    def __init__(self):
        super().__init__('qtg_engine', parameter_overrides=[
            Parameter('use_sim_time', Parameter.Type.BOOL, True),
        ])
        self.get_logger().info('qtg_engine started')


def main(args=None):
    rclpy.init(args=args)
    node = QtgEngineNode()
    rclpy.spin(node)
    rclpy.shutdown()


if __name__ == '__main__':
    main()

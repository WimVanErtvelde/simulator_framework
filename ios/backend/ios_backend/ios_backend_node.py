import rclpy
from rclpy.node import Node
from rclpy.parameter import Parameter


class IosBackendNode(Node):
    def __init__(self):
        super().__init__('ios_backend', parameter_overrides=[
            Parameter('use_sim_time', Parameter.Type.BOOL, True),
        ])
        self.get_logger().info('ios_backend started')


def main(args=None):
    rclpy.init(args=args)
    node = IosBackendNode()
    rclpy.spin(node)
    rclpy.shutdown()


if __name__ == '__main__':
    main()

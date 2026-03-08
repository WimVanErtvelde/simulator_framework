import rclpy
from rclpy.node import Node


class QtgEngineNode(Node):
    def __init__(self):
        super().__init__('qtg_engine')


def main(args=None):
    rclpy.init(args=args)
    node = QtgEngineNode()
    rclpy.spin(node)
    rclpy.shutdown()


if __name__ == '__main__':
    main()

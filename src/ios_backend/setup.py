from setuptools import find_packages, setup

package_name = 'ios_backend'

setup(
    name=package_name,
    version='0.0.1',
    packages=find_packages(exclude=['test']),
    data_files=[
        ('share/ament_index/resource_index/packages',
            ['resource/' + package_name]),
        ('share/' + package_name, ['package.xml']),
    ],
    install_requires=['setuptools'],
    zip_safe=True,
    maintainer='TODO',
    maintainer_email='todo@todo.com',
    description='IOS backend — FastAPI + rclpy bridge for the Instructor Operating Station',
    license='TODO',
    tests_require=['pytest'],
    entry_points={
        'console_scripts': [
            'ios_backend_node = ios_backend.ios_backend_node:main',
        ],
    },
)

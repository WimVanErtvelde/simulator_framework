from setuptools import find_packages, setup

package_name = 'qtg_engine'

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
    description='QTG engine — automated qualification test runner and report generator',
    license='TODO',
    tests_require=['pytest'],
    entry_points={
        'console_scripts': [
            'qtg_engine_node = qtg_engine.qtg_engine_node:main',
        ],
    },
)

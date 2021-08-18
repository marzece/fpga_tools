from distutils.core import setup, Extension
from os import getenv
module1 = Extension("fakernet_data_reader", sources = ["python_interface.c"])
setup(name= "fakernet_data_reader", version = "1.0", description="This", ext_modules = [module1])

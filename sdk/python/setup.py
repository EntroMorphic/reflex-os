from setuptools import setup

setup(
    name="reflex-os",
    version="0.1.0",
    description="Python SDK for Reflex OS nodes",
    py_modules=["reflex"],
    install_requires=["pyserial>=3.5"],
    python_requires=">=3.8",
    entry_points={
        "console_scripts": [
            "reflex-cli=reflex:main",
        ],
    },
)

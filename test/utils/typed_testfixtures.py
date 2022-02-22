#!/usr/bin/env python3

import typing
import testfixtures


def compare(*args: typing.Any, **kw: typing.Any) -> None:
    testfixtures.compare(*args, **kw)  # type: ignore


class ShouldRaise(testfixtures.ShouldRaise):  # type: ignore
    pass
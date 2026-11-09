"""aiogram 3.x: тонка обгортка над bot.commands."""
from datetime import datetime
from typing import Callable

from aiogram import F, Router
from aiogram.filters import Command, CommandObject
from aiogram.types import Message

from . import commands
from .db import Database


def build_router(
    db: Database,
    publish: commands.Publish,
    allowed_chat_id: int,
    now_fn: Callable[[], datetime] | None = None,
) -> Router:
    router = Router()
    router.message.filter(F.chat.id == allowed_chat_id)  # чужі чати ігноруємо (розділ 8.4)
    now = now_fn or (lambda: datetime.now(db.tz))

    @router.message(Command("start", "help"))
    async def cmd_help(message: Message):
        await message.answer(commands.HELP)

    @router.message(Command("focus"))
    async def cmd_focus(message: Message, command: CommandObject):
        await message.answer(await commands.focus(db, publish, command.args, now()))

    @router.message(Command("task"))
    async def cmd_task(message: Message, command: CommandObject):
        await message.answer(await commands.task(db, publish, command.args, now()))

    @router.message(Command("today"))
    async def cmd_today(message: Message):
        await message.answer(commands.today(db, now()))

    return router

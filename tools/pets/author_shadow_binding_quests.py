# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Author the acolyte's shadow binding quest line and move Malrik Duskwright into the cellar of the
Oakenshire Inn.

    py tools/pets/author_shadow_binding_quests.py            # validate only
    py tools/pets/author_shadow_binding_quests.py --apply    # write editor data

Run tools/pets/author_pet_content.py first: it owns Bind Shadow (301) and its pet.

In Alestia the dark arts are not practised in public, so Oakenshire's shadowmancer teaches in the
inn's cellar, paid for Old Harbin's silence. The quest line follows that:

    75 Beneath the Common Room   Malrik -> Old Harbin   take Harbin his coin, ask who has been asking
    76 Loose Tongues             Old Harbin -> Malrik   silence 5 Bandit Mages who asked about him
    77 A Shade of Your Own       Malrik -> Malrik       the binding

Turning in 77 raises OnQuestReward on Malrik. His trigger (117) says a line and casts Bind Shadow
on the player, which grants the Umbral Shade and teaches Summon Shadow and Dismiss Shadow. Bind
Shadow is never learned, so it does not linger in the spellbook.

The chain needs the acolyte class to be active (requiredclasses) and character level 4. It does
not follow the class unlock (38): characters created as acolytes never get that quest, which is
only offered while the class is locked. Idempotent: rows are replaced by id, quest lists of the
two NPCs are merged.

Ids (scanned free on 2026-11-02): quests end at 74, triggers at 116.
"""

import argparse
import shutil
from datetime import datetime

import author_pet_content as pets
from author_pet_content import DE, EN, FR, RU, _require, loc

pets.EDITOR_TYPES.update({
    "quests": ("quests.proto", "mmo.proto.Quests"),
    "triggers": ("triggers.proto", "mmo.proto.Triggers"),
})
NAMES = ("quests", "triggers", "units", "maps", "spells")

OUT = pets.ROOT / "generated/pets"

BENEATH_THE_COMMON_ROOM, LOOSE_TONGUES, A_SHADE_OF_YOUR_OWN = 75, 76, 77
QUEST_IDS = (BENEATH_THE_COMMON_ROOM, LOOSE_TONGUES, A_SHADE_OF_YOUR_OWN)
WHISPERS_IN_THE_DARK = 72      # Guard Emrik sends new acolytes to Malrik
BINDING_TRIGGER = 117

MALRIK, OLD_HARBIN, BANDIT_MAGE = 13, 27, 39
HUMAN_RACEMASK = 1 << 31
ACOLYTE_CLASSMASK = pets.ACOLYTE_CLASSMASK

# Cellar of the Oakenshire Inn (entity 4555430321098860313, kit floor y -1.62): local (9.3, -1.0)
# at the east wall between the wine racks and the cellar lamp, facing the cellar.
MALRIK_SPAWN_NAME = "Oakenshire Inn Cellar - Malrik Duskwright"
MALRIK_POSITION = (264.54, -1.81, 594.56)
MALRIK_ROTATION = 4.64

# trigger_event / trigger_action / target values (src/shared/proto_data/trigger_helper.h)
ON_QUEST_REWARD = 28
SAY, CAST_SPELL, DELAY = 1, 6, 7
OWNING_OBJECT, NONE = 1, 0
SPELL_TARGET_TRIGGERING_UNIT = 2

TURN_IN, KILL = 0, 2


def texts(field, de, en, fr, ru):
    return {field: en, f"{field}_loc": loc(de, en, fr, ru)}


def quest(quest_id, internal_name, minlevel, questlevel, prev, xp, money=0, quest_type=TURN_IN,
          next_chain=0, requirements=()):
    draft = {"id": quest_id, "internalname": internal_name, "minlevel": minlevel,
             "questlevel": questlevel, "type": quest_type,
             "requiredraces": HUMAN_RACEMASK, "requiredclasses": ACOLYTE_CLASSMASK,
             "rewardxp": xp, "rewardclassxp": xp, "rewardmoney": money,
             "requirements": list(requirements)}
    if prev:
        draft["prevquestid"] = prev
    if next_chain:
        draft["nextchainquestid"] = next_chain
    return draft


def quest_definitions():
    q75 = quest(BENEATH_THE_COMMON_ROOM, "Acolyte - Shadow Binding - Part 1", 4, 5, 0, 150,
                next_chain=LOOSE_TONGUES)
    q75.update(texts("name", "Unter der Schankstube", "Beneath the Common Room",
                     "Sous la salle commune", "Под общим залом"))
    q75.update(texts(
        "detailstext",
        "Du hast mich gefunden. Gut - dann kannst du ein Geheimnis bewahren, oder du lernst es "
        "noch.\n\nDort oben sehen die Leute einen Schattenwirker und greifen zur Fackel. Hier unten, "
        "zwischen Harbins Fässern, fragt niemand, wofür die Kerzen sind. Harbin sorgt dafür, und "
        "er wird dafür bezahlt.\n\nBevor ich dich lehre, einen eigenen Schemen zu binden, bring ihm "
        "die Münzen dieses Monats hinauf. Leise. Und frag ihn, ob sich jemand nach seinem Keller "
        "erkundigt hat.",
        "You found me. Good - that means you can keep a secret, or you will learn to.\n\nUp there, "
        "folk see a shadowmancer and reach for a torch. Down here, among Harbin's casks, nobody asks "
        "what the candles are for. Harbin keeps it that way, and he is paid for it.\n\nBefore I teach "
        "you to bind a shade of your own, take this month's coin up to him. Quietly. And ask him "
        "whether anyone has been asking about his cellar.",
        "Vous m'avez trouvé. Bien - c'est que vous savez garder un secret, ou que vous "
        "l'apprendrez.\n\nLà-haut, les gens voient un ombremancien et cherchent une torche. Ici, "
        "entre les fûts de Harbin, personne ne demande à quoi servent les bougies. Harbin y veille, "
        "et il est payé pour cela.\n\nAvant que je vous apprenne à lier votre propre ombre, montez-lui "
        "l'argent de ce mois. Discrètement. Et demandez-lui si quelqu'un s'est intéressé à sa cave.",
        "Ты меня нашёл. Хорошо - значит, умеешь хранить тайны или научишься.\n\nТам, наверху, "
        "люди видят теневика и хватаются за факел. Здесь, среди бочек Харбина, никто не спрашивает, "
        "зачем свечи. Харбин следит за этим, и ему за это платят.\n\nПрежде чем я научу тебя "
        "связывать собственную тень, отнеси ему наверх деньги за этот месяц. Тихо. И спроси, не "
        "интересовался ли кто-нибудь его погребом."))
    q75.update(texts(
        "objectivestext",
        "Bringt Alter Harbin in der Schankstube des Gasthauses von Oakenshire unauffällig Malriks "
        "Münzen.",
        "Quietly bring Malrik's coin to Old Harbin in the common room of the Oakenshire Inn.",
        "Apportez discrètement l'argent de Malrik au Vieux Harbin, dans la salle commune de "
        "l'auberge d'Oakenshire.",
        "Незаметно отнесите деньги Малрика Старому Харбину в общий зал трактира Оукеншира."))
    q75.update(texts(
        "offerrewardtext",
        "Steck das weg, bevor es jemand sieht. ...Danke.\n\nJa, es hat jemand gefragt. Setz dich, "
        "und sprich leise.",
        "Put that away before someone sees it. ...Thank you.\n\nYes, someone's been asking. Sit "
        "down, and keep your voice low.",
        "Rangez ça avant qu'on le voie. ...Merci.\n\nOui, quelqu'un a posé des questions. "
        "Asseyez-vous, et parlez bas.",
        "Убери это, пока никто не увидел. ...Спасибо.\n\nДа, кое-кто спрашивал. Садись и говори "
        "тише."))

    q76 = quest(LOOSE_TONGUES, "Acolyte - Shadow Binding - Part 2", 4, 5, BENEATH_THE_COMMON_ROOM,
                600, money=200, quest_type=KILL, next_chain=A_SHADE_OF_YOUR_OWN,
                requirements=[{"creatureid": BANDIT_MAGE, "creaturecount": 5}])
    q76.update(texts("name", "Lose Zungen", "Loose Tongues", "Langues déliées",
                     "Длинные языки"))
    q76.update(texts(
        "detailstext",
        "Vor zwei Nächten haben zwei Heckenzauberer aus dem Banditenlager südlich der Stadt hier "
        "bis zur Sperrstunde getrunken. Rote Armbinden, gestohlene Zauberbücher, lose Mäuler. Sie "
        "wollten wissen, wo \"der Mann unter dem Boden\" seine Lehrstunden hält.\n\nGehen sie zur "
        "Wache, verliere ich das Gasthaus, und er verliert mehr als das. Gehen sie zu ihm, ist es "
        "schlimmer - ihresgleichen stiehlt, was es sich nicht verdienen kann.\n\nGeh nach Süden, "
        "finde ihre Sorte unter den Banditen und sorg dafür, dass keiner von ihnen redet. Dann "
        "sag Malrik, was du getan hast. Nicht mir. Ich habe dich um nichts gebeten.",
        "Two nights back, a pair of hedge-wizards from the bandit camp south of town drank here "
        "till close. Red armbands, stolen spellbooks, loose mouths. They wanted to know where "
        "\"the man under the floor\" gives his lessons.\n\nIf they go to the Watch, I lose the inn, "
        "and he loses more than that. If they go to him, it's worse - their sort steal what they "
        "can't earn.\n\nGo south, find their kind among the bandits, and make sure none of them "
        "talk. Then tell Malrik what you did. Not me. I didn't ask you for anything.",
        "Il y a deux nuits, deux sorciers de haie du camp de bandits au sud de la ville ont bu "
        "ici jusqu'à la fermeture. Brassards rouges, grimoires volés, langues bien pendues. Ils "
        "voulaient savoir où \"l'homme sous le plancher\" donne ses leçons.\n\nS'ils vont voir la "
        "Garde, je perds l'auberge, et lui perd bien plus. S'ils vont le voir, c'est pire - leurs "
        "semblables volent ce qu'ils ne peuvent mériter.\n\nAllez au sud, trouvez leurs pareils "
        "parmi les bandits et faites en sorte qu'aucun ne parle. Puis dites à Malrik ce que vous "
        "avez fait. Pas à moi. Je ne vous ai rien demandé.",
        "Две ночи назад пара доморощенных колдунов из лагеря бандитов к югу от города пила здесь "
        "до закрытия. Красные повязки, краденые книги заклинаний, длинные языки. Они хотели "
        "знать, где \"человек под полом\" даёт свои уроки.\n\nЕсли они пойдут к страже, я потеряю "
        "трактир, а он - куда больше. Если пойдут к нему, будет хуже: такие крадут то, чего не "
        "могут заслужить.\n\nСтупай на юг, найди их братию среди бандитов и позаботься, чтобы "
        "никто из них не проболтался. Потом расскажи Малрику, что сделал. Не мне. Я тебя ни о "
        "чём не просил."))
    q76.update(texts(
        "objectivestext",
        "Bringt 5 Banditenmagier im Banditenlager südlich von Oakenshire zum Schweigen und "
        "erstattet dann Malrik Dämmerschmied im Keller des Gasthauses Bericht.",
        "Silence 5 Bandit Mages in the bandit camp south of Oakenshire, then report to Malrik "
        "Duskwright in the inn's cellar.",
        "Réduisez au silence 5 Mages bandits dans le camp de bandits au sud d'Oakenshire, puis "
        "faites votre rapport à Malrik Forge-Crépuscule dans la cave de l'auberge.",
        "Заставьте замолчать 5 бандитов-магов в лагере бандитов к югу от Оукеншира, затем "
        "доложите Малрику Сумеречнику в погребе трактира."))
    q76.update(texts(
        "requestitemstext",
        "Nun? Sind sie still?", "Well? Are they quiet?", "Alors ? Se sont-ils tus ?",
        "Ну? Они молчат?"))
    q76.update(texts(
        "offerrewardtext",
        "Harbin sorgt sich. Deshalb vertraue ich ihm.\n\nDu hast es gespürt, als sie fielen, nicht "
        "wahr - denselben Sog wie bei den Wölfen. Gut. Genau davon nährt sich ein Schemen. Deiner "
        "ist schon hungrig.",
        "Harbin worries. It is why I trust him.\n\nYou felt it when they fell, didn't you - the same "
        "pull you felt among the wolves. Good. That is exactly what a shade feeds on. Yours is "
        "hungry already.",
        "Harbin s'inquiète. C'est pour cela que je lui fais confiance.\n\nVous l'avez senti quand "
        "ils sont tombés, n'est-ce pas - la même attraction que parmi les loups. Bien. C'est "
        "exactement ce dont une ombre se nourrit. La vôtre a déjà faim.",
        "Харбин тревожится. Потому я ему и доверяю.\n\nТы почувствовал это, когда они падали, "
        "верно? То же притяжение, что и среди волков. Хорошо. Именно этим питается тень. Твоя "
        "уже голодна."))

    q77 = quest(A_SHADE_OF_YOUR_OWN, "Acolyte - Shadow Binding - Part 3", 4, 5, LOOSE_TONGUES, 250)
    q77.update(texts("name", "Ein eigener Schemen", "A Shade of Your Own", "Une ombre à soi",
                     "Собственная тень"))
    q77.update(texts(
        "detailstext",
        "Jeder Akolyth wirft einen Schatten. Die meisten bemerken nie, dass er zurückblickt.\n\n"
        "Heute Nacht lernt deiner deinen Namen. Stell dich in den Kreis, halt die Hände still und "
        "sprich nicht, während ich arbeite. Danach gehorcht der Schemen dir und niemandem sonst - "
        "ruf ihn, wenn du ihn brauchst, und schick ihn zurück in die Dunkelheit, wenn nicht.\n\n"
        "Sag mir, wenn du bereit bist.",
        "Every acolyte casts a shadow. Most never notice it looking back.\n\nTonight yours learns "
        "your name. Stand inside the circle, keep your hands still, and do not speak while I work. "
        "When it is done, the shade answers to you and no one else - call it when you need it, and "
        "send it back to the dark when you do not.\n\nTell me when you are ready.",
        "Chaque acolyte projette une ombre. La plupart ne remarquent jamais qu'elle les "
        "regarde.\n\nCette nuit, la vôtre apprend votre nom. Placez-vous dans le cercle, gardez les "
        "mains immobiles et ne parlez pas pendant que je travaille. Ensuite, l'ombre n'obéira qu'à "
        "vous - appelez-la quand vous en avez besoin, et renvoyez-la dans les ténèbres sinon.\n\n"
        "Dites-moi quand vous serez prêt.",
        "Каждый послушник отбрасывает тень. Большинство так и не замечает, что она смотрит в "
        "ответ.\n\nСегодня твоя узнает твоё имя. Встань в круг, держи руки неподвижно и молчи, "
        "пока я работаю. Когда всё закончится, тень будет слушаться тебя и никого больше: зови "
        "её, когда нужна, и отсылай обратно во тьму, когда нет.\n\nСкажи, когда будешь готов."))
    q77.update(texts(
        "objectivestext",
        "Sagt Malrik Dämmerschmied, wenn Ihr bereit seid, Euren Schemen zu binden.",
        "Tell Malrik Duskwright when you are ready to bind your shade.",
        "Dites à Malrik Forge-Crépuscule quand vous serez prêt à lier votre ombre.",
        "Скажите Малрику Сумеречнику, когда будете готовы связать свою тень."))
    q77.update(texts(
        "offerrewardtext",
        "Dann halt still. Es wird kalt.", "Then hold still. This will be cold.",
        "Alors ne bougez plus. Ce sera froid.", "Тогда не двигайся. Будет холодно."))

    return [q75, q76, q77]


def whispers_in_the_dark_texts():
    """Quest 72 used to send new acolytes to the Town Hall."""
    result = texts(
        "detailstext",
        "Ihr bemerkt Dinge in den Schatten, an denen andere achtlos vorbeigehen, $N. Eine solche "
        "Gabe ist nützlich, doch ungeschult wendet sie sich gern gegen den, der sie trägt.\n\n"
        "Malrik Dämmerschmied unterrichtet die Akolythen von Oakenshire dort, wo niemand hinsieht: "
        "im Keller unter dem Gasthaus. Alter Harbin wird nicht bemerken, wie Ihr hinabsteigt. "
        "Sprecht mit ihm, bevor Ihr Euch tiefer in den Wald wagt.",
        "You notice things in the shadows that others walk right past, $N. That kind of gift is "
        "useful, but left untrained it tends to turn on whoever carries it.\n\nMalrik Duskwright "
        "teaches Oakenshire's acolytes where nobody looks: the cellar beneath the inn. Old Harbin "
        "will not see you go down. Speak with him before you venture deeper into the forest.",
        "Vous remarquez dans les ombres des choses que les autres ne voient pas, $N. Un tel don est "
        "utile, mais sans formation, il finit souvent par se retourner contre celui qui le porte.\n\n"
        "Malrik Forge-Crépuscule forme les acolytes d'Oakenshire là où personne ne regarde : dans "
        "la cave sous l'auberge. Le Vieux Harbin ne vous verra pas descendre. Parlez-lui avant de "
        "vous aventurer plus profondément dans la forêt.",
        "Вы замечаете в тенях то, мимо чего другие проходят не глядя, $N. Такой дар полезен, но "
        "без обучения он нередко обращается против своего владельца.\n\nМалрик Сумеречник обучает "
        "аколитов Океншира там, куда никто не смотрит: в погребе под трактиром. Старый Харбин не "
        "заметит, как вы спуститесь. Поговорите с ним, прежде чем углубляться в лес.")
    result.update(texts(
        "objectivestext",
        "Sprecht mit Malrik Dämmerschmied im Keller des Gasthauses von Oakenshire.",
        "Speak with Malrik Duskwright in the cellar of the Oakenshire Inn.",
        "Parlez à Malrik Forge-Crépuscule dans la cave de l'auberge d'Oakenshire.",
        "Поговорите с Малриком Сумеречником в погребе трактира Океншира."))
    return result


def binding_trigger():
    return {
        "id": BINDING_TRIGGER, "name": "Malrik Duskwright - Bind Shadow on turn-in",
        "newevents": [{"type": ON_QUEST_REWARD, "data": [A_SHADE_OF_YOUR_OWN]}],
        "actions": [
            {"action": SAY, "target": OWNING_OBJECT, **texts(
                "texts", "Schatten, hier ist dein Herr. Lerne den Namen.",
                "Shadow, here is your master. Learn the name.",
                "Ombre, voici ton maître. Apprends son nom.",
                "Тень, вот твой хозяин. Запомни имя.")},
            {"action": CAST_SPELL, "target": OWNING_OBJECT,
             "data": [pets.BIND_SHADOW, SPELL_TARGET_TRIGGERING_UNIT]},
            {"action": DELAY, "target": NONE, "data": [3500]},
            {"action": SAY, "target": OWNING_OBJECT, **texts(
                "texts", "Es ist vollbracht. Ruf ihn, und er wird kommen.",
                "It is done. Call it, and it will come.",
                "C'est fait. Appelez-la, et elle viendra.",
                "Готово. Позови её, и она придёт.")},
        ],
    }


# --- Upserts ---------------------------------------------------------------------------------

def _texts_field(draft):
    """TriggerAction texts are repeated; texts() made it a single string."""
    for action in draft["actions"]:
        if "texts" in action:
            action["texts"] = [action["texts"]]
    return draft


def upsert_rows(dataset, drafts, kind):
    by_id = {row.id: row for row in dataset.entry}
    for draft in drafts:
        target = by_id.get(draft["id"])
        if target is None:
            target = dataset.entry.add()
        else:
            _require(target.name == draft["name"],
                     f"{kind} {draft['id']} is {target.name!r}, refusing to overwrite it with "
                     f"{draft['name']!r}")
        target.Clear()
        pets.json_format.ParseDict(draft, target)


def add_unique(repeated, values):
    for value in values:
        if value not in repeated:
            repeated.append(value)


def wire_npcs(units):
    by_id = {unit.id: unit for unit in units.entry}
    malrik, harbin = by_id.get(MALRIK), by_id.get(OLD_HARBIN)
    _require(malrik is not None and malrik.name == "Malrik Duskwright", "unit 13 is not Malrik")
    _require(harbin is not None and harbin.name == "Old Harbin", "unit 27 is not Old Harbin")
    add_unique(malrik.quests, [BENEATH_THE_COMMON_ROOM, A_SHADE_OF_YOUR_OWN])
    add_unique(malrik.end_quests, [LOOSE_TONGUES, A_SHADE_OF_YOUR_OWN])
    add_unique(malrik.triggers, [BINDING_TRIGGER])
    add_unique(harbin.quests, [LOOSE_TONGUES])
    add_unique(harbin.end_quests, [BENEATH_THE_COMMON_ROOM])


def move_malrik(maps):
    world = next((entry for entry in maps.entry if entry.id == 0), None)
    _require(world is not None, "map 0 is missing")
    spawns = [spawn for spawn in world.unitspawns if spawn.unitentry == MALRIK]
    _require(len(spawns) == 1, f"expected one Malrik spawn, found {len(spawns)}")
    spawn = spawns[0]
    spawn.name = MALRIK_SPAWN_NAME
    spawn.positionx, spawn.positiony, spawn.positionz = MALRIK_POSITION
    spawn.rotation = MALRIK_ROTATION


def update_whispers(quests):
    q72 = next((entry for entry in quests.entry if entry.id == WHISPERS_IN_THE_DARK), None)
    _require(q72 is not None and q72.name == "Whispers in the Dark", "quest 72 is missing")
    for field, value in whispers_in_the_dark_texts().items():
        if field.endswith("_loc"):
            del getattr(q72, field)[:]
            for entry in value:
                getattr(q72, field).add(**entry)
        else:
            setattr(q72, field, value)


# --- Validation ------------------------------------------------------------------------------

def validate(data):
    quests = {q.id: q for q in data["quests"].entry}
    units = {u.id: u for u in data["units"].entry}
    triggers = {t.id: t for t in data["triggers"].entry}
    spells = {s.id: s for s in data["spells"].entry}

    for name in NAMES:
        ids = [row.id for row in data[name].entry]
        _require(len(ids) == len(set(ids)), f"duplicate ids in {name}")
        _require(data[name].IsInitialized(), f"{name} dataset is missing required fields")

    providers = {qid: [u.id for u in units.values() if qid in u.quests] for qid in QUEST_IDS}
    enders = {qid: [u.id for u in units.values() if qid in u.end_quests] for qid in QUEST_IDS}
    _require(providers == {75: [MALRIK], 76: [OLD_HARBIN], 77: [MALRIK]}, f"providers {providers}")
    _require(enders == {75: [OLD_HARBIN], 76: [MALRIK], 77: [MALRIK]}, f"enders {enders}")

    for qid in QUEST_IDS + (WHISPERS_IN_THE_DARK,):
        q = quests[qid]
        for field in ("name_loc", "detailstext_loc", "objectivestext_loc"):
            _require(pets._locales(getattr(q, field)) == [DE, EN, FR, RU],
                     f"quest {qid}.{field} lacks a locale")
        _require("Town Hall" not in q.detailstext + q.objectivestext,
                 f"quest {qid} still sends players to the Town Hall")
    for qid in QUEST_IDS:
        _require(pets._locales(quests[qid].offerrewardtext_loc) == [DE, EN, FR, RU],
                 f"quest {qid}.offerrewardtext_loc lacks a locale")
        prev = quests[qid].prevquestid
        _require(prev == 0 or prev in quests, f"quest {qid} follows an unknown quest")
    for requirement in quests[LOOSE_TONGUES].requirements:
        _require(requirement.creatureid in units, "Loose Tongues names an unknown creature")

    trigger = triggers[BINDING_TRIGGER]
    _require(BINDING_TRIGGER in units[MALRIK].triggers, "Malrik does not own the binding trigger")
    casts = [a for a in trigger.actions if a.action == CAST_SPELL]
    _require(len(casts) == 1 and list(casts[0].data) == [pets.BIND_SHADOW,
                                                          SPELL_TARGET_TRIGGERING_UNIT],
             "the binding trigger must cast Bind Shadow at the triggering player")
    for action in trigger.actions:
        if action.action == SAY:
            _require(pets._locales(action.texts_loc) == [DE, EN, FR, RU], "a Say lacks a locale")

    bind = spells.get(pets.BIND_SHADOW)
    _require(bind is not None and bind.name == "Bind Shadow", "Bind Shadow is missing")
    _require(all(e.targeta == pets.TARGET_ANY for e in bind.effects),
             "Bind Shadow must target the player it is cast on (run author_pet_content.py)")

    world = next(entry for entry in data["maps"].entry if entry.id == 0)
    spawn = next(s for s in world.unitspawns if s.unitentry == MALRIK)
    _require(spawn.name == MALRIK_SPAWN_NAME, "Malrik was not moved to the cellar")


def build():
    data = {name: pets.load_editor(name) for name in NAMES}
    upsert_rows(data["quests"], quest_definitions(), "quest")
    upsert_rows(data["triggers"], [_texts_field(binding_trigger())], "trigger")
    update_whispers(data["quests"])
    wire_npcs(data["units"])
    move_malrik(data["maps"])
    validate(data)
    return data


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()

    data = build()
    if not args.apply:
        print(f"validated quests {QUEST_IDS}, trigger {BINDING_TRIGGER}, quest "
              f"{WHISPERS_IN_THE_DARK} texts and Malrik's cellar spawn")
        return

    OUT.mkdir(parents=True, exist_ok=True)
    backup = OUT / ("backup_binding_" + datetime.now().strftime("%Y%m%d_%H%M%S"))
    backup.mkdir()
    changed = []
    for name, dataset in data.items():
        path = pets.editor_path(name)
        shutil.copy2(path, backup / f"editor_{name}.data")
        blob = dataset.SerializeToString()
        if path.read_bytes() != blob:
            path.write_bytes(blob)
            changed.append(name)
        # maps and spells are shared with the client; keep ClientDB in step like mmo_edit does.
        if name in pets.CLIENT_SHARED and pets.client_path(name).read_bytes() != blob:
            pets.client_path(name).write_bytes(blob)

    print(f"wrote {', '.join(changed) or 'nothing'} to the editor data. Backup: {backup}")


if __name__ == "__main__":
    main()

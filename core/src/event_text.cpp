#include "rules/event.h"

#include "rules/names.h"
#include "text_encoding.h"

#include <sstream>

namespace rules {

namespace {

void writeInts(std::ostringstream& s, const char* name, const std::vector<int>& values) {
	if (values.empty()) {
		return;
	}
	s << ' ' << name << '=';
	for (std::size_t i = 0; i < values.size(); ++i) {
		s << (i ? "," : "") << values[i];
	}
}

void writeStrings(std::ostringstream& s, const char* name, const std::vector<std::string>& values) {
	if (!values.empty()) {
		s << ' ' << name << '=' << text::encodeList(values);
	}
}

}

std::string serializeEvent(const Event& e) {
	const Event d;
	std::ostringstream s;
	s << e.round << ' ' << toString(e.type);
	if (e.actor != d.actor) {
		s << " actor=" << e.actor;
	}
	if (e.target != d.target) {
		s << " target=" << e.target;
	}
	if (e.previousTarget != d.previousTarget) {
		s << " previous_target=" << e.previousTarget;
	}
	if (e.ability != d.ability) {
		s << " ability=" << text::encode(e.ability);
	}
	if (e.combo != d.combo) {
		s << " combo=" << text::encode(e.combo);
	}
	if (e.status != d.status) {
		s << " status=" << text::encode(e.status);
	}
	if (e.message != d.message) {
		s << " message=" << text::encode(e.message);
	}
	writeStrings(s, "params", e.params);
	if (e.amount != d.amount) {
		s << " amount=" << e.amount;
	}
	if (e.hp != d.hp) {
		s << " hp=" << e.hp;
	}
	if (e.duration != d.duration) {
		s << " duration=" << e.duration;
	}
	if (e.command != d.command) {
		s << " command=" << toString(e.command);
	}
	if (e.element != d.element) {
		s << " element=" << toString(e.element);
	}
	if (e.effectiveness != d.effectiveness) {
		s << " effectiveness=" << toString(e.effectiveness);
	}
	if (e.stat != d.stat) {
		s << " stat=" << toString(e.stat);
	}
	if (e.cancel != d.cancel) {
		s << " cancel=" << toString(e.cancel);
	}
	if (e.statusEnd != d.statusEnd) {
		s << " status_end=" << toString(e.statusEnd);
	}
	if (e.outcome != d.outcome) {
		s << " outcome=" << toString(e.outcome);
	}
	if (e.resource != d.resource) {
		s << " resource=" << toString(e.resource);
	}
	if (e.opening != d.opening) {
		s << " opening=" << toString(e.opening);
	}
	if (e.succeeded) {
		s << " succeeded=1";
	}
	if (e.gameOver) {
		s << " game_over=1";
	}
	if (e.experience != d.experience) {
		s << " experience=" << e.experience;
	}
	if (e.currency != d.currency) {
		s << " currency=" << e.currency;
	}
	if (e.macro != d.macro) {
		s << " macro=" << text::encode(e.macro);
	}
	writeInts(s, "participants", e.participants);
	writeInts(s, "order", e.order);
	writeStrings(s, "ids", e.ids);
	writeStrings(s, "groups", e.groups);
	if (e.partyCount != d.partyCount) {
		s << " party_count=" << e.partyCount;
	}
	if (e.flags.escapeAllowed != d.flags.escapeAllowed || e.flags.defeatHasBranch != d.flags.defeatHasBranch || e.flags.vehicleBattle != d.flags.vehicleBattle) {
		s << " flags=escape:" << e.flags.escapeAllowed << ",defeat_branch:" << e.flags.defeatHasBranch << ",vehicle:" << e.flags.vehicleBattle;
	}
	if (!e.commands.empty()) {
		s << " commands=";
		for (std::size_t i = 0; i < e.commands.size(); ++i) {
			const RecordedCommand& c = e.commands[i];
			s << (i ? "," : "") << c.actor << ':' << toString(c.command.kind) << ':' << text::encode(c.command.id) << ':' << c.command.target;
		}
	}
	return s.str();
}

std::string serializeEvents(const std::vector<Event>& events) {
	std::string out;
	for (const Event& e : events) {
		out += serializeEvent(e);
		out += '\n';
	}
	return out;
}

}

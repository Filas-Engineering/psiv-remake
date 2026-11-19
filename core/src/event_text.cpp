#include "rules/event.h"

#include "rules/names.h"
#include "text_encoding.h"

#include <sstream>

namespace rules {

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
	if (e.ability != d.ability) {
		s << " ability=" << text::encode(e.ability);
	}
	if (e.status != d.status) {
		s << " status=" << text::encode(e.status);
	}
	if (e.amount != d.amount) {
		s << " amount=" << e.amount;
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
	if (e.fail != d.fail) {
		s << " fail=" << toString(e.fail);
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
	if (e.succeeded) {
		s << " succeeded=1";
	}
	if (e.firstDiscovery) {
		s << " first_discovery=1";
	}
	if (!e.participants.empty()) {
		s << " participants=";
		for (std::size_t i = 0; i < e.participants.size(); ++i) {
			s << (i ? "," : "") << e.participants[i];
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

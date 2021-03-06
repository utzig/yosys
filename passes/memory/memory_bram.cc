/*
 *  yosys -- Yosys Open SYnthesis Suite
 *
 *  Copyright (C) 2012  Clifford Wolf <clifford@clifford.at>
 *  
 *  Permission to use, copy, modify, and/or distribute this software for any
 *  purpose with or without fee is hereby granted, provided that the above
 *  copyright notice and this permission notice appear in all copies.
 *  
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include "kernel/yosys.h"

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

struct rules_t
{
	struct portinfo_t {
		int group, index, dupidx;
		int wrmode, enable, transp, clocks, clkpol;

		SigBit sig_clock;
		SigSpec sig_addr, sig_data, sig_en;
		bool effective_clkpol;
		int mapped_port;
	};

	struct bram_t {
		IdString name;
		int groups, abits, dbits, init;
		vector<int> ports, wrmode, enable, transp, clocks, clkpol;

		vector<portinfo_t> make_portinfos() const
		{
			vector<portinfo_t> portinfos;
			for (int i = 0; i < groups && i < GetSize(ports); i++)
			for (int j = 0; j < ports[i]; j++) {
				portinfo_t pi;
				pi.group = i;
				pi.index = j;
				pi.dupidx = 0;
				pi.wrmode = i < GetSize(wrmode) ? wrmode[i] : 0;
				pi.enable = i < GetSize(enable) ? enable[i] : 0;
				pi.transp = i < GetSize(transp) ? transp[i] : 0;
				pi.clocks = i < GetSize(clocks) ? clocks[i] : 0;
				pi.clkpol = i < GetSize(clkpol) ? clkpol[i] : 0;
				pi.mapped_port = -1;
				portinfos.push_back(pi);
			}
			return portinfos;
		}
	};

	struct match_t {
		IdString name;
		dict<string, int> min_limits, max_limits;
		bool or_next_if_better;
		int shuffle_enable;
	};

	dict<IdString, bram_t> brams;
	vector<match_t> matches;

	std::ifstream infile;
	vector<string> tokens;
	int linecount;

	void syntax_error()
	{
		if (tokens.empty())
			log_error("Unexpected end of rules file in line %d.\n", linecount);
		log_error("Syntax error in rules file line %d.\n", linecount);
	}

	bool next_line()
	{
		linecount++;
		tokens.clear();
		string line;
		while (std::getline(infile, line)) {
			for (string tok = next_token(line); !tok.empty(); tok = next_token(line)) {
				if (tok[0] == '#')
					break;
				tokens.push_back(tok);
			}
			if (!tokens.empty())
				return true;
		}
		return false;
	}

	bool parse_single_int(const char *stmt, int &value)
	{
		if (GetSize(tokens) == 2 && tokens[0] == stmt) {
			value = atoi(tokens[1].c_str());
			return true;
		}
		return false;
	}

	bool parse_int_vect(const char *stmt, vector<int> &value)
	{
		if (GetSize(tokens) >= 2 && tokens[0] == stmt) {
			value.resize(GetSize(tokens)-1);
			for (int i = 1; i < GetSize(tokens); i++)
				value[i-1] = atoi(tokens[i].c_str());
			return true;
		}
		return false;
	}

	void parse_bram()
	{
		if (GetSize(tokens) != 2)
			syntax_error();

		bram_t data;
		data.name = RTLIL::escape_id(tokens[1]);

		while (next_line())
		{
			if (GetSize(tokens) == 1 && tokens[0] == "endbram") {
				brams[data.name] = data;
				return;
			}

			if (parse_single_int("groups", data.groups))
				continue;

			if (parse_single_int("abits", data.abits))
				continue;

			if (parse_single_int("dbits", data.dbits))
				continue;

			if (parse_single_int("init", data.init))
				continue;

			if (parse_int_vect("ports", data.ports))
				continue;

			if (parse_int_vect("wrmode", data.wrmode))
				continue;

			if (parse_int_vect("enable", data.enable))
				continue;

			if (parse_int_vect("transp", data.transp))
				continue;

			if (parse_int_vect("clocks", data.clocks))
				continue;

			if (parse_int_vect("clkpol", data.clkpol))
				continue;

			break;
		}

		syntax_error();
	}

	void parse_match()
	{
		if (GetSize(tokens) != 2)
			syntax_error();

		match_t data;
		data.name = RTLIL::escape_id(tokens[1]);
		data.or_next_if_better = false;
		data.shuffle_enable = 0;

		while (next_line())
		{
			if (GetSize(tokens) == 1 && tokens[0] == "endmatch") {
				matches.push_back(data);
				return;
			}

			if (GetSize(tokens) == 3 && tokens[0] == "min") {
				data.min_limits[tokens[1]] = atoi(tokens[2].c_str());
				continue;
			}

			if (GetSize(tokens) == 3 && tokens[0] == "max") {
				data.max_limits[tokens[1]] = atoi(tokens[2].c_str());
				continue;
			}

			if (GetSize(tokens) == 2 && tokens[0] == "shuffle_enable") {
				data.shuffle_enable = atoi(tokens[1].c_str());
				continue;
			}

			if (GetSize(tokens) == 1 && tokens[0] == "or_next_if_better") {
				data.or_next_if_better = true;
				continue;
			}

			break;
		}

		syntax_error();
	}

	void parse(string filename)
	{
		if (filename.substr(0, 2) == "+/")
			filename = proc_share_dirname() + filename.substr(1);

		infile.open(filename);
		linecount = 0;

		if (infile.fail())
			log_error("Can't open rules file `%s'.\n", filename.c_str());

		while (next_line())
		{
			if (tokens[0] == "bram") parse_bram();
			else if (tokens[0] == "match") parse_match();
			else syntax_error();
		}

		infile.close();
	}
};

bool replace_cell(Cell *cell, const rules_t::bram_t &bram, const rules_t::match_t &match, dict<string, int> &match_properties, int mode)
{
	Module *module = cell->module;

	auto portinfos = bram.make_portinfos();
	int dup_count = 1;

	dict<int, pair<SigBit, bool>> clock_domains;
	dict<int, bool> clock_polarities;
	dict<int, bool> read_transp;
	pool<int> clocks_wr_ports;
	pool<int> clkpol_wr_ports;
	int clocks_max = 0;
	int clkpol_max = 0;
	int transp_max = 0;

	clock_polarities[0] = false;
	clock_polarities[1] = true;

	for (auto &pi : portinfos) {
		if (pi.wrmode) {
			clocks_wr_ports.insert(pi.clocks);
			if (pi.clkpol > 1)
				clkpol_wr_ports.insert(pi.clkpol);
		}
		clocks_max = std::max(clocks_max, pi.clocks);
		clkpol_max = std::max(clkpol_max, pi.clkpol);
		transp_max = std::max(transp_max, pi.transp);
	}

	log("    Mapping to bram type %s:\n", log_id(bram.name));

	int mem_size = cell->getParam("\\SIZE").as_int();
	int mem_abits = cell->getParam("\\ABITS").as_int();
	int mem_width = cell->getParam("\\WIDTH").as_int();
	// int mem_offset = cell->getParam("\\OFFSET").as_int();

	int wr_ports = cell->getParam("\\WR_PORTS").as_int();
	auto wr_clken = SigSpec(cell->getParam("\\WR_CLK_ENABLE"));
	auto wr_clkpol = SigSpec(cell->getParam("\\WR_CLK_POLARITY"));
	wr_clken.extend_u0(wr_ports);
	wr_clkpol.extend_u0(wr_ports);

	SigSpec wr_en = cell->getPort("\\WR_EN");
	SigSpec wr_clk = cell->getPort("\\WR_CLK");
	SigSpec wr_data = cell->getPort("\\WR_DATA");
	SigSpec wr_addr = cell->getPort("\\WR_ADDR");

	int rd_ports = cell->getParam("\\RD_PORTS").as_int();
	auto rd_clken = SigSpec(cell->getParam("\\RD_CLK_ENABLE"));
	auto rd_clkpol = SigSpec(cell->getParam("\\RD_CLK_POLARITY"));
	auto rd_transp = SigSpec(cell->getParam("\\RD_TRANSPARENT"));
	rd_clken.extend_u0(rd_ports);
	rd_clkpol.extend_u0(rd_ports);
	rd_transp.extend_u0(rd_ports);

	SigSpec rd_clk = cell->getPort("\\RD_CLK");
	SigSpec rd_data = cell->getPort("\\RD_DATA");
	SigSpec rd_addr = cell->getPort("\\RD_ADDR");

	if (match.shuffle_enable && bram.dbits >= match.shuffle_enable*2)
	{
		int bucket_size = bram.dbits / match.shuffle_enable;
		log("      Shuffle bit order to accommodate enable buckets of size %d..\n", bucket_size);

		// extract unshuffled data/enable bits

		std::vector<SigSpec> old_wr_en;
		std::vector<SigSpec> old_wr_data;
		std::vector<SigSpec> old_rd_data;

		for (int i = 0; i < wr_ports; i++) {
			old_wr_en.push_back(wr_en.extract(i*mem_width, mem_width));
			old_wr_data.push_back(wr_data.extract(i*mem_width, mem_width));
		}

		for (int i = 0; i < rd_ports; i++)
			old_rd_data.push_back(rd_data.extract(i*mem_width, mem_width));

		// analyze enable structure

		std::vector<SigSpec> en_order;
		dict<SigSpec, vector<int>> bits_wr_en;

		for (int i = 0; i < mem_width; i++) {
			SigSpec sig;
			for (int j = 0; j < wr_ports; j++)
				sig.append(old_wr_en[j][i]);
			if (bits_wr_en.count(sig) == 0)
				en_order.push_back(sig);
			bits_wr_en[sig].push_back(i);
		}

		// re-create memory ports

		std::vector<SigSpec> new_wr_en(GetSize(old_wr_en));
		std::vector<SigSpec> new_wr_data(GetSize(old_wr_data));
		std::vector<SigSpec> new_rd_data(GetSize(old_rd_data));
		std::vector<int> shuffle_map;

		for (auto &it : en_order)
		{
			auto &bits = bits_wr_en.at(it);
			int buckets = (GetSize(bits) + bucket_size - 1) / bucket_size;
			int fillbits = buckets*bucket_size - GetSize(bits);
			SigBit fillbit;

			for (int i = 0; i < GetSize(bits); i++) {
				for (int j = 0; j < wr_ports; j++) {
					new_wr_en[j].append(old_wr_en[j][bits[i]]);
					new_wr_data[j].append(old_wr_data[j][bits[i]]);
					fillbit = old_wr_en[j][bits[i]];
				}
				for (int j = 0; j < rd_ports; j++)
					new_rd_data[j].append(old_rd_data[j][bits[i]]);
				shuffle_map.push_back(bits[i]);
			}

			for (int i = 0; i < fillbits; i++) {
				for (int j = 0; j < wr_ports; j++) {
					new_wr_en[j].append(fillbit);
					new_wr_data[j].append(State::S0);
				}
				for (int j = 0; j < rd_ports; j++)
					new_rd_data[j].append(State::Sx);
				shuffle_map.push_back(-1);
			}
		}

		log("      Results of bit order shuffling:");
		for (int v : shuffle_map)
			log(" %d", v);
		log("\n");

		// update mem_*, wr_*, and rd_* variables

		mem_width = GetSize(new_wr_en.front());
		wr_en = SigSpec(0, wr_ports * mem_width);
		wr_data = SigSpec(0, wr_ports * mem_width);
		rd_data = SigSpec(0, rd_ports * mem_width);

		for (int i = 0; i < wr_ports; i++) {
			wr_en.replace(i*mem_width, new_wr_en[i]);
			wr_data.replace(i*mem_width, new_wr_data[i]);
		}

		for (int i = 0; i < rd_ports; i++)
			rd_data.replace(i*mem_width, new_rd_data[i]);
	}

	// assign write ports

	for (int cell_port_i = 0, bram_port_i = 0; cell_port_i < wr_ports; cell_port_i++)
	{
		bool clken = wr_clken[cell_port_i] == State::S1;
		bool clkpol = wr_clkpol[cell_port_i] == State::S1;
		SigBit clksig = wr_clk[cell_port_i];

		pair<SigBit, bool> clkdom(clksig, clkpol);
		if (!clken)
			clkdom = pair<SigBit, bool>(State::S1, false);

		log("      Write port #%d is in clock domain %s%s.\n",
				cell_port_i, clkdom.second ? "" : "!",
				clken ? log_signal(clkdom.first) : "~async~");

		for (; bram_port_i < GetSize(portinfos); bram_port_i++)
		{
			auto &pi = portinfos[bram_port_i];

			if (pi.wrmode != 1)
		skip_bram_wport:
				continue;

			if (clken) {
				if (pi.clocks == 0) {
					log("        Bram port %c%d has incompatible clock type.\n", pi.group + 'A', pi.index + 1);
					goto skip_bram_wport;
				}
				if (clock_domains.count(pi.clocks) && clock_domains.at(pi.clocks) != clkdom) {
					log("        Bram port %c%d is in a different clock domain.\n", pi.group + 'A', pi.index + 1);
					goto skip_bram_wport;
				}
				if (clock_polarities.count(pi.clkpol) && clock_polarities.at(pi.clkpol) != clkpol) {
					log("        Bram port %c%d has incompatible clock polarity.\n", pi.group + 'A', pi.index + 1);
					goto skip_bram_wport;
				}
			} else {
				if (pi.clocks != 0) {
					log("        Bram port %c%d has incompatible clock type.\n", pi.group + 'A', pi.index + 1);
					goto skip_bram_wport;
				}
			}

			SigSpec sig_en;
			SigBit last_en_bit = State::S1;
			for (int i = 0; i < mem_width; i++) {
				if (pi.enable && i % (bram.dbits / pi.enable) == 0) {
					last_en_bit = wr_en[i + cell_port_i*mem_width];
					sig_en.append(last_en_bit);
				}
				if (last_en_bit != wr_en[i + cell_port_i*mem_width]) {
					log("        Bram port %c%d has incompatible enable structure.\n", pi.group + 'A', pi.index + 1);
					goto skip_bram_wport;
				}
			}

			log("        Mapped to bram port %c%d.\n", pi.group + 'A', pi.index + 1);
			pi.mapped_port = cell_port_i;

			if (clken) {
				clock_domains[pi.clocks] = clkdom;
				clock_polarities[pi.clkpol] = clkdom.second;
				pi.sig_clock = clkdom.first;
				pi.effective_clkpol = clkdom.second;
			}

			pi.sig_en = sig_en;
			pi.sig_addr = wr_addr.extract(cell_port_i*mem_abits, mem_abits);
			pi.sig_data = wr_data.extract(cell_port_i*mem_width, mem_width);

			bram_port_i++;
			goto mapped_wr_port;
		}

		log("        Failed to map write port #%d.\n", cell_port_i);
		return false;
	mapped_wr_port:;
	}

	// houskeeping stuff for growing more read ports and restarting read port assignments

	int grow_read_ports_cursor = -1;
	bool try_growing_more_read_ports = false;
	auto backup_clock_domains = clock_domains;
	auto backup_clock_polarities = clock_polarities;

	if (0) {
grow_read_ports:;
		vector<rules_t::portinfo_t> new_portinfos;
		for (auto &pi : portinfos) {
			if (pi.wrmode == 0) {
				pi.mapped_port = -1;
				pi.sig_clock = SigBit();
				pi.sig_addr = SigSpec();
				pi.sig_data = SigSpec();
				pi.sig_en = SigSpec();
			}
			new_portinfos.push_back(pi);
			if (pi.dupidx == dup_count-1) {
				if (pi.clocks && !clocks_wr_ports[pi.clocks])
					pi.clocks += clocks_max;
				if (pi.clkpol > 1 && !clkpol_wr_ports[pi.clkpol])
					pi.clkpol += clkpol_max;
				if (pi.transp > 1)
					pi.transp += transp_max;
				pi.dupidx++;
				new_portinfos.push_back(pi);
			}
		}
		try_growing_more_read_ports = false;
		portinfos.swap(new_portinfos);
		clock_domains = backup_clock_domains;
		clock_polarities = backup_clock_polarities;
		dup_count++;
	}

	read_transp.clear();
	read_transp[0] = false;
	read_transp[1] = true;

	// assign read ports

	for (int cell_port_i = 0; cell_port_i < rd_ports; cell_port_i++)
	{
		bool clken = rd_clken[cell_port_i] == State::S1;
		bool clkpol = rd_clkpol[cell_port_i] == State::S1;
		bool transp = rd_transp[cell_port_i] == State::S1;
		SigBit clksig = rd_clk[cell_port_i];

		pair<SigBit, bool> clkdom(clksig, clkpol);
		if (!clken)
			clkdom = pair<SigBit, bool>(State::S1, false);

		log("      Read port #%d is in clock domain %s%s.\n",
				cell_port_i, clkdom.second ? "" : "!",
				clken ? log_signal(clkdom.first) : "~async~");

		for (int bram_port_i = 0; bram_port_i < GetSize(portinfos); bram_port_i++)
		{
			auto &pi = portinfos[bram_port_i];

			if (pi.wrmode != 0 || pi.mapped_port >= 0)
		skip_bram_rport:
				continue;

			if (clken) {
				if (pi.clocks == 0) {
					log("        Bram port %c%d.%d has incompatible clock type.\n", pi.group + 'A', pi.index + 1, pi.dupidx + 1);
					goto skip_bram_rport;
				}
				if (clock_domains.count(pi.clocks) && clock_domains.at(pi.clocks) != clkdom) {
					log("        Bram port %c%d.%d is in a different clock domain.\n", pi.group + 'A', pi.index + 1, pi.dupidx + 1);
					goto skip_bram_rport;
				}
				if (clock_polarities.count(pi.clkpol) && clock_polarities.at(pi.clkpol) != clkpol) {
					log("        Bram port %c%d.%d has incompatible clock polarity.\n", pi.group + 'A', pi.index + 1, pi.dupidx + 1);
					goto skip_bram_rport;
				}
				if (read_transp.count(pi.transp) && read_transp.at(pi.transp) != transp) {
					log("        Bram port %c%d.%d has incompatible read transparancy.\n", pi.group + 'A', pi.index + 1, pi.dupidx + 1);
					goto skip_bram_rport;
				}
			} else {
				if (pi.clocks != 0) {
					log("        Bram port %c%d.%d has incompatible clock type.\n", pi.group + 'A', pi.index + 1, pi.dupidx + 1);
					goto skip_bram_rport;
				}
			}

			log("        Mapped to bram port %c%d.%d.\n", pi.group + 'A', pi.index + 1, pi.dupidx + 1);
			pi.mapped_port = cell_port_i;

			if (clken) {
				clock_domains[pi.clocks] = clkdom;
				clock_polarities[pi.clkpol] = clkdom.second;
				read_transp[pi.transp] = transp;
				pi.sig_clock = clkdom.first;
				pi.effective_clkpol = clkdom.second;
			}

			pi.sig_addr = rd_addr.extract(cell_port_i*mem_abits, mem_abits);
			pi.sig_data = rd_data.extract(cell_port_i*mem_width, mem_width);

			if (grow_read_ports_cursor < cell_port_i) {
				grow_read_ports_cursor = cell_port_i;
				try_growing_more_read_ports = true;
			}

			goto mapped_rd_port;
		}

		log("        Failed to map read port #%d.\n", cell_port_i);
		if (try_growing_more_read_ports) {
			log("      Growing more read ports by duplicating bram cells.\n");
			goto grow_read_ports;
		}
		return false;
	mapped_rd_port:;
	}

	// update properties and re-check conditions

	if (mode <= 1)
	{
		match_properties["dups"] = dup_count;
		match_properties["waste"] = match_properties["dups"] * match_properties["bwaste"];

		int cells = ((mem_width + bram.dbits - 1) / bram.dbits) * ((mem_size + (1 << bram.abits) - 1) / (1 << bram.abits));
		match_properties["efficiency"] = (100 * match_properties["bits"]) / (dup_count * cells * bram.dbits * (1 << bram.abits));

		log("      Updated properties: dups=%d waste=%d efficiency=%d\n",
				match_properties["dups"], match_properties["waste"], match_properties["efficiency"]);

		for (auto it : match.min_limits) {
			if (!match_properties.count(it.first))
				log_error("Unknown property '%s' in match rule for bram type %s.\n",
						it.first.c_str(), log_id(match.name));
			if (match_properties[it.first] >= it.second)
				continue;
			log("    Rule for bram type %s rejected: requirement 'min %s %d' not met.\n",
					log_id(match.name), it.first.c_str(), it.second);
			return false;
		}
		for (auto it : match.max_limits) {
			if (!match_properties.count(it.first))
				log_error("Unknown property '%s' in match rule for bram type %s.\n",
						it.first.c_str(), log_id(match.name));
			if (match_properties[it.first] <= it.second)
				continue;
			log("    Rule for bram type %s rejected: requirement 'max %s %d' not met.\n",
					log_id(match.name), it.first.c_str(), it.second);
			return false;
		}

		if (mode == 1)
			return true;
	}

	// actually replace that memory cell

	dict<SigSpec, pair<SigSpec, SigSpec>> dout_cache;

	for (int grid_d = 0; grid_d*bram.dbits < mem_width; grid_d++)
	for (int grid_a = 0; grid_a*(1 << bram.abits) < mem_size; grid_a++)
	for (int dupidx = 0; dupidx < dup_count; dupidx++)
	{
		Cell *c = module->addCell(module->uniquify(stringf("%s.%d.%d.%d", cell->name.c_str(), grid_d, grid_a, dupidx)), bram.name);
		log("      Creating %s cell at grid position <%d %d %d>: %s\n", log_id(bram.name), grid_d, grid_a, dupidx, log_id(c));

		for (auto &pi : portinfos)
		{
			if (pi.dupidx != dupidx)
				continue;

			string prefix = stringf("%c%d", pi.group + 'A', pi.index + 1);
			const char *pf = prefix.c_str();

			if (pi.clocks && (!c->hasPort(stringf("\\CLK%d", (pi.clocks-1) % clocks_max + 1)) || pi.sig_clock.wire)) {
				c->setPort(stringf("\\CLK%d", (pi.clocks-1) % clocks_max + 1), pi.sig_clock);
				if (pi.clkpol > 1 && pi.sig_clock.wire)
					c->setParam(stringf("\\CLKPOL%d", (pi.clkpol-1) % clkpol_max + 1), clock_polarities.at(pi.clkpol));
				if (pi.transp > 1 && pi.sig_clock.wire)
					c->setParam(stringf("\\TRANSP%d", (pi.transp-1) % transp_max + 1), read_transp.at(pi.transp));
			}

			SigSpec addr_ok;
			if (GetSize(pi.sig_addr) > bram.abits) {
				SigSpec extra_addr = pi.sig_addr.extract(bram.abits, GetSize(pi.sig_addr) - bram.abits);
				SigSpec extra_addr_sel = SigSpec(grid_a, GetSize(extra_addr));
				addr_ok = module->Eq(NEW_ID, extra_addr, extra_addr_sel);
			}

			if (pi.enable)
			{
				SigSpec sig_en = pi.sig_en;
				sig_en.extend_u0((grid_d+1) * pi.enable);
				sig_en = sig_en.extract(grid_d * pi.enable, pi.enable);

				if (!addr_ok.empty())
					sig_en = module->Mux(NEW_ID, SigSpec(0, GetSize(sig_en)), sig_en, addr_ok);

				c->setPort(stringf("\\%sEN", pf), sig_en);
			}

			SigSpec sig_data = pi.sig_data;
			sig_data.extend_u0((grid_d+1) * bram.dbits);
			sig_data = sig_data.extract(grid_d * bram.dbits, bram.dbits);

			if (pi.wrmode == 1) {
				c->setPort(stringf("\\%sDATA", pf), sig_data);
			} else {
				SigSpec bram_dout = module->addWire(NEW_ID, bram.dbits);
				c->setPort(stringf("\\%sDATA", pf), bram_dout);

				for (int i = bram.dbits-1; i >= 0; i--)
					if (sig_data[i].wire == nullptr) {
						sig_data.remove(i);
						bram_dout.remove(i);
					}

				SigSpec addr_ok_q = addr_ok;
				if (pi.clocks && !addr_ok.empty()) {
					addr_ok_q = module->addWire(NEW_ID);
					module->addDff(NEW_ID, pi.sig_clock, addr_ok, addr_ok_q, pi.effective_clkpol);
				}

				dout_cache[sig_data].first.append(addr_ok_q);
				dout_cache[sig_data].second.append(bram_dout);
			}

			SigSpec sig_addr = pi.sig_addr;
			sig_addr.extend_u0(bram.abits);
			c->setPort(stringf("\\%sADDR", pf), sig_addr);
		}
	}

	for (auto &it : dout_cache)
	{
		if (it.second.first.empty())
		{
			log_assert(GetSize(it.first) == GetSize(it.second.second));
			module->connect(it.first, it.second.second);
		}
		else
		{
			log_assert(GetSize(it.first)*GetSize(it.second.first) == GetSize(it.second.second));
			module->addPmux(NEW_ID, SigSpec(State::Sx, GetSize(it.first)), it.second.second, it.second.first, it.first);
		}
	}

	module->remove(cell);
	return true;
}

void handle_cell(Cell *cell, const rules_t &rules)
{
	log("Processing %s.%s:\n", log_id(cell->module), log_id(cell));

	dict<string, int> match_properties;
	match_properties["words"]  = cell->getParam("\\SIZE").as_int();
	match_properties["abits"]  = cell->getParam("\\ABITS").as_int();
	match_properties["dbits"]  = cell->getParam("\\WIDTH").as_int();
	match_properties["wports"] = cell->getParam("\\WR_PORTS").as_int();
	match_properties["rports"] = cell->getParam("\\RD_PORTS").as_int();
	match_properties["bits"]   = match_properties["words"] * match_properties["dbits"];
	match_properties["ports"]  = match_properties["wports"] + match_properties["rports"];

	log("  Properties:");
	for (auto &it : match_properties)
		log(" %s=%d", it.first.c_str(), it.second);
	log("\n");

	pool<IdString> failed_brams;
	dict<int, int> best_rule_cache;

	for (int i = 0; i < GetSize(rules.matches); i++)
	{
		if (!rules.brams.count(rules.matches[i].name))
			log_error("No bram description for resource %s found!\n", log_id(rules.matches[i].name));

		auto &match = rules.matches.at(i);
		auto &bram = rules.brams.at(match.name);

		if (match.name.in(failed_brams))
			continue;

		int avail_rd_ports = 0;
		int avail_wr_ports = 0;
		for (int j = 0; j < bram.groups; j++) {
			if (GetSize(bram.wrmode) < j || bram.wrmode.at(j) == 0)
				avail_rd_ports += GetSize(bram.ports) < j ? bram.ports.at(j) : 0;
			if (GetSize(bram.wrmode) < j || bram.wrmode.at(j) != 0)
				avail_wr_ports += GetSize(bram.ports) < j ? bram.ports.at(j) : 0;
		}

		log("  Checking rule #%d for bram type %s:\n", i, log_id(match.name));
		log("    Bram geometry: abits=%d dbits=%d wports=%d rports=%d\n", bram.abits, bram.dbits, avail_wr_ports, avail_rd_ports);

		int dups = avail_rd_ports ? (match_properties["rports"] + avail_rd_ports - 1) / avail_rd_ports : 1;
		match_properties["dups"] = dups;

		log("    Estimated number of duplicates for more read ports: dups=%d\n", match_properties["dups"]);

		int aover = match_properties["words"] % (1 << bram.abits);
		int awaste = aover ? (1 << bram.abits) - aover : 0;
		match_properties["awaste"] = awaste;

		int dover = match_properties["dbits"] % bram.dbits;
		int dwaste = dover ? bram.dbits - dover : 0;
		match_properties["dwaste"] = dwaste;

		int bwaste = awaste * bram.dbits + dwaste * (1 << bram.abits) - awaste * dwaste;
		match_properties["bwaste"] = bwaste;

		int waste = match_properties["dups"] * bwaste;
		match_properties["waste"] = waste;

		int cells = ((match_properties["dbits"] + bram.dbits - 1) / bram.dbits) * ((match_properties["words"] + (1 << bram.abits) - 1) / (1 << bram.abits));
		int efficiency = (100 * match_properties["bits"]) / (dups * cells * bram.dbits * (1 << bram.abits));
		match_properties["efficiency"] = efficiency;

		log("    Metrics for %s: awaste=%d dwaste=%d bwaste=%d waste=%d efficiency=%d\n",
				log_id(match.name), awaste, dwaste, bwaste, waste, efficiency);

		for (auto it : match.min_limits) {
			if (it.first == "waste" || it.first == "dups")
				continue;
			if (!match_properties.count(it.first))
				log_error("Unknown property '%s' in match rule for bram type %s.\n",
						it.first.c_str(), log_id(match.name));
			if (match_properties[it.first] >= it.second)
				continue;
			log("    Rule #%d for bram type %s rejected: requirement 'min %s %d' not met.\n",
					i, log_id(match.name), it.first.c_str(), it.second);
			goto next_match_rule;
		}
		for (auto it : match.max_limits) {
			if (!match_properties.count(it.first))
				log_error("Unknown property '%s' in match rule for bram type %s.\n",
						it.first.c_str(), log_id(match.name));
			if (match_properties[it.first] <= it.second)
				continue;
			log("    Rule #%d for bram type %s rejected: requirement 'max %s %d' not met.\n",
					i, log_id(match.name), it.first.c_str(), it.second);
			goto next_match_rule;
		}

		log("    Rule #%d for bram type %s accepted.\n", i, log_id(match.name));

		if (match.or_next_if_better || !best_rule_cache.empty())
		{
			if (match.or_next_if_better && i+1 == GetSize(rules.matches))
				log_error("Found 'or_next_if_better' in last match rule.\n");

			if (!replace_cell(cell, bram, match, match_properties, 1)) {
				log("    Mapping to bram type %s failed.\n", log_id(match.name));
				failed_brams.insert(match.name);
				goto next_match_rule;
			}

			log("      Storing for later selection.\n");
			best_rule_cache[i] = match_properties["efficiency"];

			if (match.or_next_if_better)
				goto next_match_rule;

	next_match_rule:
			if (match.or_next_if_better || best_rule_cache.empty())
				continue;

			log("  Selecting best of %d rules:\n", GetSize(best_rule_cache));
			int best_rule = best_rule_cache.begin()->first;

			for (auto &it : best_rule_cache) {
				if (it.second > best_rule_cache[best_rule])
					best_rule = it.first;
				log("    Efficiency for rule %d: %d\n", it.first, it.second);
			}

			log("    Selected rule %d with efficiency %d.\n", best_rule, best_rule_cache[best_rule]);
			best_rule_cache.clear();

			if (!replace_cell(cell, rules.brams.at(rules.matches.at(best_rule).name), rules.matches.at(best_rule), match_properties, 2))
				log_error("Mapping to bram type %s after pre-selection failed.\n", log_id(rules.matches.at(best_rule).name));
			return;
		}

		if (!replace_cell(cell, bram, match, match_properties, 0)) {
			log("    Mapping to bram type %s failed.\n", log_id(match.name));
			failed_brams.insert(match.name);
			goto next_match_rule;
		}
		return;
	}

	log("  No acceptable bram resources found.\n");
}

struct MemoryBramPass : public Pass {
	MemoryBramPass() : Pass("memory_bram", "map memories to block rams") { }
	virtual void help()
	{
		//   |---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|
		log("\n");
		log("    memory_bram -rules <rule_file> [selection]\n");
		log("\n");
		log("This pass converts the multi-port $mem memory cells into block ram instances.\n");
		log("The given rules file describes the available resources and how they should be\n");
		log("used.\n");
		log("\n");
		log("The rules file contains a set of block ram description and a sequence of match\n");
		log("rules. A block ram description looks like this:\n");
		log("\n");
		log("    bram RAMB1024X32     # name of BRAM cell\n");
		// log("      init 1             # set to '1' if BRAM can be initialized\n");
		log("      abits 10           # number of address bits\n");
		log("      dbits 32           # number of data bits\n");
		log("      groups 2           # number of port groups\n");
		log("      ports  1 1         # number of ports in each group\n");
		log("      wrmode 1 0         # set to '1' if this groups is write ports\n");
		log("      enable 4 0         # number of enable bits (for write ports)\n");
		log("      transp 0 2         # transparatent (for read ports)\n");
		log("      clocks 1 2         # clock configuration\n");
		log("      clkpol 2 2         # clock polarity configuration\n");
		log("    endbram\n");
		log("\n");
		log("For the option 'transp' the value 0 means non-transparent, 1 means transparent\n");
		log("and a value greater than 1 means configurable. All groups with the same\n");
		log("value greater than 1 share the same configuration bit.\n");
		log("\n");
		log("For the option 'clocks' the value 0 means non-clocked, and a value greater\n");
		log("than 0 means clocked. All groups with the same value share the same clock\n");
		log("signal.\n");
		log("\n");
		log("For the option 'clkpol' the value 0 means negative edge, 1 means positive edge\n");
		log("and a value greater than 1 means configurable. All groups with the same value\n");
		log("greater than 1 share the same configuration bit.\n");
		log("\n");
		log("A match rule looks like this:\n");
		log("\n");
		log("    match RAMB1024X32\n");
		log("      max waste 16384    # only use this bram if <= 16k ram bits are unused\n");
		log("      min efficiency 80  # only use this bram if efficiency is at least 80%%\n");
		log("    endmatch\n");
		log("\n");
		log("It is possible to match against the following values with min/max rules:\n");
		log("\n");
		log("    words  ........  number of words in memory in design\n");
		log("    abits  ........  number of adress bits on memory in design\n");
		log("    dbits  ........  number of data bits on memory in design\n");
		log("    wports  .......  number of write ports on memory in design\n");
		log("    rports  .......  number of read ports on memory in design\n");
		log("    ports  ........  number of ports on memory in design\n");
		log("    bits  .........  number of bits in memory in design\n");
		log("    dups ..........  number of duplications for more read ports\n");
		log("\n");
		log("    awaste  .......  number of unused address slots for this match\n");
		log("    dwaste  .......  number of unused data bits for this match\n");
		log("    bwaste  .......  number of unused bram bits for this match\n");
		log("    waste  ........  total number of unused bram bits (bwaste*dups)\n");
		log("    efficiency  ...  total percentage of used and non-duplicated bits\n");
		log("\n");
		log("The interface for the created bram instances is dervived from the bram\n");
		log("description. Use 'techmap' to convert the created bram instances into\n");
		log("instances of the actual bram cells of your target architecture.\n");
		log("\n");
		log("A match containing the command 'or_next_if_better' is only used if it\n");
		log("has a higher efficiency than the next match (and the one after that if\n");
		log("the next also has 'or_next_if_better' set, and so forth).\n");
		log("\n");
		log("A match containing the command 'shuffle_enable <N>' will re-organize\n");
		log("the data bits to accommodate bram ports with <N> enable bits before\n");
		log("mapping to the bram.\n");
		log("\n");
	}
	virtual void execute(vector<string> args, Design *design)
	{
		rules_t rules;

		log_header("Executing MEMORY_BRAM pass (mapping $mem cells to block memories).\n");

		size_t argidx;
		for (argidx = 1; argidx < args.size(); argidx++) {
			if (args[argidx] == "-rules" && argidx+1 < args.size()) {
				rules.parse(args[++argidx]);
				continue;
			}
			break;
		}
		extra_args(args, argidx, design);

		for (auto mod : design->selected_modules())
		for (auto cell : mod->selected_cells())
			if (cell->type == "$mem")
				handle_cell(cell, rules);
	}
} MemoryBramPass;

PRIVATE_NAMESPACE_END

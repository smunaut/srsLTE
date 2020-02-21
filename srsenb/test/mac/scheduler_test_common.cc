/*
 * Copyright 2013-2019 Software Radio Systems Limited
 *
 * This file is part of srsLTE.
 *
 * srsLTE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * srsLTE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * A copy of the GNU Affero General Public License can be found in
 * the LICENSE file in the top-level directory of this distribution
 * and at http://www.gnu.org/licenses/.
 *
 */

#include "scheduler_test_common.h"
#include "lib/include/srslte/common/pdu.h"
#include "srsenb/hdr/stack/mac/scheduler.h"

#include "srslte/common/test_common.h"

#include <set>

using namespace srsenb;

int output_sched_tester::test_pusch_collisions(const tti_params_t&                    tti_params,
                                               const sched_interface::ul_sched_res_t& ul_result,
                                               prbmask_t&                             ul_allocs) const
{
  uint32_t nof_prb = cell_params.nof_prb();
  ul_allocs.resize(nof_prb);
  ul_allocs.reset();

  auto try_ul_fill = [&](srsenb::ul_harq_proc::ul_alloc_t alloc, const char* ch_str, bool strict = true) {
    CONDERROR((alloc.RB_start + alloc.L) > nof_prb,
              "[TESTER] Allocated RBs (%d,%d) out-of-bounds\n",
              alloc.RB_start,
              alloc.RB_start + alloc.L);
    CONDERROR(alloc.L == 0, "[TESTER] Allocations must have at least one PRB\n");
    if (strict and ul_allocs.any(alloc.RB_start, alloc.RB_start + alloc.L)) {
      TESTERROR("[TESTER] Collision Detected of %s alloc=(%d,%d) and cumulative_mask=0x%s\n",
                ch_str,
                alloc.RB_start,
                alloc.RB_start + alloc.L,
                ul_allocs.to_hex().c_str());
    }
    ul_allocs.fill(alloc.RB_start, alloc.RB_start + alloc.L, true);
    return SRSLTE_SUCCESS;
  };

  /* TEST: Check if there is space for PRACH */
  bool is_prach_tti_tx_ul =
      srslte_prach_tti_opportunity_config_fdd(cell_params.cfg.prach_config, tti_params.tti_tx_ul, -1);
  if (is_prach_tti_tx_ul) {
    try_ul_fill({cell_params.cfg.prach_freq_offset, 6}, "PRACH");
  }

  /* TEST: check collisions in PUCCH */
  bool strict = nof_prb != 6 or (not is_prach_tti_tx_ul); // and not tti_data.ul_pending_msg3_present);
  try_ul_fill({0, (uint32_t)cell_params.cfg.nrb_pucch}, "PUCCH", strict);
  try_ul_fill(
      {cell_params.cfg.cell.nof_prb - cell_params.cfg.nrb_pucch, (uint32_t)cell_params.cfg.nrb_pucch}, "PUCCH", strict);

  /* TEST: check collisions in the UL PUSCH */
  for (uint32_t i = 0; i < ul_result.nof_dci_elems; ++i) {
    uint32_t L, RBstart;
    srslte_ra_type2_from_riv(ul_result.pusch[i].dci.type2_alloc.riv, &L, &RBstart, nof_prb, nof_prb);
    strict = ul_result.pusch[i].needs_pdcch or nof_prb != 6; // Msg3 may collide with PUCCH at PRB==6
    try_ul_fill({RBstart, L}, "PUSCH", strict);
    //    ue_stats[ul_result.pusch[i].dci.rnti].nof_ul_rbs += L;
  }

  return SRSLTE_SUCCESS;
}

int output_sched_tester::test_pdsch_collisions(const tti_params_t&                    tti_params,
                                               const sched_interface::dl_sched_res_t& dl_result,
                                               rbgmask_t&                             rbgmask) const
{
  srslte::bounded_bitset<100, true> dl_allocs(cell_params.cfg.cell.nof_prb), alloc_mask(cell_params.cfg.cell.nof_prb);

  auto try_dl_mask_fill = [&](const srslte_dci_dl_t& dci, const char* channel) {
    if (extract_dl_prbmask(cell_params.cfg.cell, dci, &alloc_mask) != SRSLTE_SUCCESS) {
      return SRSLTE_ERROR;
    }
    if ((dl_allocs & alloc_mask).any()) {
      TESTERROR("[TESTER] Detected collision in the DL %s allocation (%s intersects %s)\n",
                channel,
                dl_allocs.to_string().c_str(),
                alloc_mask.to_string().c_str());
    }
    dl_allocs |= alloc_mask;
    return SRSLTE_SUCCESS;
  };

  // Decode BC allocations, check collisions, and fill cumulative mask
  for (uint32_t i = 0; i < dl_result.nof_bc_elems; ++i) {
    TESTASSERT(try_dl_mask_fill(dl_result.bc[i].dci, "BC") == SRSLTE_SUCCESS);
  }

  // Decode RAR allocations, check collisions, and fill cumulative mask
  for (uint32_t i = 0; i < dl_result.nof_rar_elems; ++i) {
    TESTASSERT(try_dl_mask_fill(dl_result.rar[i].dci, "RAR") == SRSLTE_SUCCESS);
  }

  // forbid Data in DL if it conflicts with PRACH for PRB==6
  if (cell_params.cfg.cell.nof_prb == 6) {
    uint32_t tti_rx_ack = TTI_RX_ACK(tti_params.tti_rx);
    if (srslte_prach_tti_opportunity_config_fdd(cell_params.cfg.prach_config, tti_rx_ack, -1)) {
      dl_allocs.fill(0, dl_allocs.size());
    }
  }

  // Decode Data allocations, check collisions and fill cumulative mask
  for (uint32_t i = 0; i < dl_result.nof_data_elems; ++i) {
    TESTASSERT(try_dl_mask_fill(dl_result.data[i].dci, "data") == SRSLTE_SUCCESS);
  }

  // TEST: check for holes in the PRB mask (RBGs not fully filled)
  rbgmask.resize(cell_params.nof_rbgs);
  rbgmask.reset();
  srslte::bounded_bitset<100, true> rev_alloc = ~dl_allocs;
  for (uint32_t i = 0; i < cell_params.nof_rbgs; ++i) {
    uint32_t lim = SRSLTE_MIN((i + 1) * cell_params.P, dl_allocs.size());
    bool     val = dl_allocs.any(i * cell_params.P, lim);
    CONDERROR(rev_alloc.any(i * cell_params.P, lim) and val, "[TESTER] No holes can be left in an RBG\n");
    if (val) {
      rbgmask.set(i);
    }
  }

  return SRSLTE_SUCCESS;
}

int output_sched_tester::test_sib_scheduling(const tti_params_t&                    tti_params,
                                             const sched_interface::dl_sched_res_t& dl_result) const
{
  uint32_t sfn          = tti_params.sfn;
  uint32_t sf_idx       = tti_params.sf_idx;
  bool     sib1_present = ((sfn % 2) == 0) and sf_idx == 5;

  using bc_elem     = const sched_interface::dl_sched_bc_t;
  bc_elem* bc_begin = &dl_result.bc[0];
  bc_elem* bc_end   = &dl_result.bc[dl_result.nof_bc_elems];

  /* Test if SIB1 was correctly scheduled */
  if (sib1_present) {
    auto it = std::find_if(bc_begin, bc_end, [](bc_elem& elem) { return elem.index == 0; });
    CONDERROR(it == bc_end, "Failed to allocate SIB1 in even sfn, sf_idx==5\n");
  }

  /* Test if any SIB was scheduled with wrong index, tbs, or outside of its window */
  for (bc_elem* bc = bc_begin; bc != bc_end; ++bc) {
    if (bc->index == 0) {
      continue;
    }
    CONDERROR(bc->index >= sched_interface::MAX_SIBS, "Invalid SIB idx=%d\n", bc->index + 1);
    CONDERROR(bc->tbs < cell_params.cfg.sibs[bc->index].len,
              "Allocated BC process with TBS=%d < sib_len=%d\n",
              bc->tbs,
              cell_params.cfg.sibs[bc->index].len);
    uint32_t x         = (bc->index - 1) * cell_params.cfg.si_window_ms;
    uint32_t sf        = x % 10;
    uint32_t sfn_start = sfn;
    while ((sfn_start % cell_params.cfg.sibs[bc->index].period_rf) != x / 10) {
      sfn_start--;
    }
    uint32_t win_start = sfn_start * 10 + sf;
    uint32_t win_end   = win_start + cell_params.cfg.si_window_ms;
    CONDERROR(tti_params.tti_tx_dl < win_start or tti_params.tti_tx_dl > win_end,
              "Scheduled SIB is outside of its SIB window\n");
  }
  return SRSLTE_SUCCESS;
}

int output_sched_tester::test_pdcch_collisions(const sched_interface::dl_sched_res_t& dl_result,
                                               const sched_interface::ul_sched_res_t& ul_result,
                                               srslte::bounded_bitset<128, true>*     used_cce) const
{
  used_cce->resize(srslte_regs_pdcch_ncce(cell_params.regs.get(), dl_result.cfi));
  used_cce->reset();

  // Helper Function: checks if there is any collision. If not, fills the PDCCH mask
  auto try_cce_fill = [&](const srslte_dci_location_t& dci_loc, const char* ch) {
    uint32_t cce_start = dci_loc.ncce, cce_stop = dci_loc.ncce + (1u << dci_loc.L);
    if (used_cce->any(cce_start, cce_stop)) {
      TESTERROR("[TESTER] %s DCI collision between CCE positions (%u, %u)\n", ch, cce_start, cce_stop);
    }
    used_cce->fill(cce_start, cce_stop);
    return SRSLTE_SUCCESS;
  };

  /* TEST: verify there are no dci collisions for UL, DL data, BC, RAR */
  for (uint32_t i = 0; i < ul_result.nof_dci_elems; ++i) {
    const auto& pusch = ul_result.pusch[i];
    if (not pusch.needs_pdcch) {
      // In case of non-adaptive retx or Msg3
      continue;
    }
    try_cce_fill(pusch.dci.location, "UL");
  }
  for (uint32_t i = 0; i < dl_result.nof_data_elems; ++i) {
    try_cce_fill(dl_result.data[i].dci.location, "DL data");
  }
  for (uint32_t i = 0; i < dl_result.nof_bc_elems; ++i) {
    try_cce_fill(dl_result.bc[i].dci.location, "DL BC");
  }
  for (uint32_t i = 0; i < dl_result.nof_rar_elems; ++i) {
    try_cce_fill(dl_result.rar[i].dci.location, "DL RAR");
  }

  return SRSLTE_SUCCESS;
}

int output_sched_tester::test_dci_values_consistency(const sched_interface::dl_sched_res_t& dl_result,
                                                     const sched_interface::ul_sched_res_t& ul_result) const
{
  for (uint32_t i = 0; i < ul_result.nof_dci_elems; ++i) {
    const auto& pusch = ul_result.pusch[i];
    CONDERROR(pusch.tbs == 0, "Allocated RAR process with invalid TBS=%d\n", pusch.tbs);
    //    CONDERROR(ue_db.count(pusch.dci.rnti) == 0, "The allocated rnti=0x%x does not exist\n", pusch.dci.rnti);
    if (not pusch.needs_pdcch) {
      // In case of non-adaptive retx or Msg3
      continue;
    }
    CONDERROR(pusch.dci.location.L == 0,
              "[TESTER] Invalid aggregation level %d\n",
              pusch.dci.location.L); // TODO: Extend this test
  }
  for (uint32_t i = 0; i < dl_result.nof_data_elems; ++i) {
    auto& data = dl_result.data[i];
    CONDERROR(data.tbs[0] == 0, "Allocated DL data has empty TBS\n");
  }
  for (uint32_t i = 0; i < dl_result.nof_bc_elems; ++i) {
    auto& bc = dl_result.bc[i];
    if (bc.type == sched_interface::dl_sched_bc_t::BCCH) {
      CONDERROR(bc.tbs < cell_params.cfg.sibs[bc.index].len,
                "Allocated BC process with TBS=%d < sib_len=%d\n",
                bc.tbs,
                cell_params.cfg.sibs[bc.index].len);
    } else if (bc.type == sched_interface::dl_sched_bc_t::PCCH) {
      CONDERROR(bc.tbs == 0, "Allocated paging process with invalid TBS=%d\n", bc.tbs);
    } else {
      TESTERROR("Invalid broadcast process id=%d\n", (int)bc.type);
    }
  }
  for (uint32_t i = 0; i < dl_result.nof_rar_elems; ++i) {
    const auto& rar = dl_result.rar[i];
    CONDERROR(rar.tbs == 0, "Allocated RAR process with invalid TBS=%d\n", rar.tbs);
  }

  return SRSLTE_SUCCESS;
}

int output_sched_tester::test_all(const tti_params_t&                    tti_params,
                                  const sched_interface::dl_sched_res_t& dl_result,
                                  const sched_interface::ul_sched_res_t& ul_result) const
{
  prbmask_t ul_allocs;
  TESTASSERT(test_pusch_collisions(tti_params, ul_result, ul_allocs) == SRSLTE_SUCCESS);
  rbgmask_t dl_mask;
  TESTASSERT(test_pdsch_collisions(tti_params, dl_result, dl_mask) == SRSLTE_SUCCESS);
  TESTASSERT(test_sib_scheduling(tti_params, dl_result) == SRSLTE_SUCCESS);
  srslte::bounded_bitset<128, true> used_cce;
  TESTASSERT(test_pdcch_collisions(dl_result, ul_result, &used_cce) == SRSLTE_SUCCESS);
  return SRSLTE_SUCCESS;
}

int srsenb::extract_dl_prbmask(const srslte_cell_t&               cell,
                               const srslte_dci_dl_t&             dci,
                               srslte::bounded_bitset<100, true>* alloc_mask)
{
  srslte_pdsch_grant_t grant;
  srslte_dl_sf_cfg_t   dl_sf    = {};
  srslte_dci_dl_t*     dci_dyn  = const_cast<srslte_dci_dl_t*>(&dci); // TODO
  srslte_cell_t*       cell_dyn = const_cast<srslte_cell_t*>(&cell);

  alloc_mask->resize(cell.nof_prb);
  alloc_mask->reset();

  CONDERROR(srslte_ra_dl_dci_to_grant(cell_dyn, &dl_sf, SRSLTE_TM1, false, dci_dyn, &grant) == SRSLTE_ERROR,
            "Failed to decode PDSCH grant\n");
  for (uint32_t j = 0; j < alloc_mask->size(); ++j) {
    if (grant.prb_idx[0][j]) {
      alloc_mask->set(j);
    }
  }
  return SRSLTE_SUCCESS;
}

int user_state_sched_tester::add_user(uint16_t                                 rnti,
                                      uint32_t                                 preamble_idx,
                                      const srsenb::sched_interface::ue_cfg_t& ue_cfg)
{
  TESTASSERT(users.count(rnti) == 0);
  ue_state ue;
  ue.user_cfg     = ue_cfg;
  ue.prach_tti    = tti_params.tti_rx;
  ue.preamble_idx = preamble_idx;
  users.insert(std::make_pair(rnti, ue));
  return SRSLTE_SUCCESS;
}

int user_state_sched_tester::user_reconf(uint16_t rnti, const srsenb::sched_interface::ue_cfg_t& ue_cfg)
{
  TESTASSERT(users.count(rnti) > 0);
  users[rnti].user_cfg = ue_cfg;
  return SRSLTE_SUCCESS;
}

int user_state_sched_tester::bearer_cfg(uint16_t                                        rnti,
                                        uint32_t                                        lcid,
                                        const srsenb::sched_interface::ue_bearer_cfg_t& bearer_cfg)
{
  auto it = users.find(rnti);
  TESTASSERT(it != users.end());
  it->second.user_cfg.ue_bearers[lcid] = bearer_cfg;
  users[rnti].drb_cfg_flag             = false;
  for (uint32_t i = 2; i < it->second.user_cfg.ue_bearers.size(); ++i) {
    if (it->second.user_cfg.ue_bearers[i].direction != sched_interface::ue_bearer_cfg_t::IDLE) {
      users[rnti].drb_cfg_flag = true;
    }
  }
  return SRSLTE_SUCCESS;
}

void user_state_sched_tester::rem_user(uint16_t rnti)
{
  users.erase(rnti);
}

/**
 * Tests whether the RAR and Msg3 were scheduled within the expected windows. Individual tests:
 * - a user does not get UL allocs before Msg3
 * - a user does not get DL data allocs before Msg3 is correctly received
 * - a user RAR alloc falls within its RAR window
 * - There is only one RAR in the RAR window for a given user
 * - Msg3 is allocated in expected TTI, without PDCCH, and correct rnti
 * - First Data allocation happens after Msg3, and contains a ConRes
 * - No RARs are allocated with wrong enb_cc_idx, preamble_idx or wrong user
 * TODO:
 * - check Msg3 PRBs match the ones advertised in the RAR
 * - space is enough for Msg3
 */
int user_state_sched_tester::test_ra(uint32_t                               enb_cc_idx,
                                     const sched_interface::dl_sched_res_t& dl_result,
                                     const sched_interface::ul_sched_res_t& ul_result)
{
  uint32_t msg3_count = 0;

  for (auto& iter : users) {
    uint16_t  rnti     = iter.first;
    ue_state& userinfo = iter.second;

    // No UL allocations before Msg3
    for (uint32_t i = 0; i < ul_result.nof_dci_elems; ++i) {
      if (ul_result.pusch[i].dci.rnti == rnti) {
        CONDERROR(ul_result.pusch[i].needs_pdcch and userinfo.msg3_tti < 0,
                  "[TESTER] No UL data allocation allowed before Msg3\n");
        CONDERROR(userinfo.rar_tti < 0, "[TESTER] No UL allocation allowed before RAR\n");
        uint32_t msg3_tti = (uint32_t)(userinfo.rar_tti + FDD_HARQ_DELAY_MS + MSG3_DELAY_MS) % 10240;
        CONDERROR(msg3_tti > tti_params.tti_tx_ul, "No UL allocs allowed before Msg3 alloc\n");
      }
    }

    // No DL data allocations before Msg3 is received
    for (uint32_t i = 0; i < dl_result.nof_data_elems; ++i) {
      if (dl_result.data[i].dci.rnti == rnti) {
        CONDERROR(userinfo.msg3_tti < 0, "[TESTER] No DL data alloc allowed before Msg3 alloc\n");
        CONDERROR(tti_params.tti_rx < (uint32_t)userinfo.msg3_tti,
                  "[TESTER] Msg4 cannot be tx without Msg3 being received\n");
      }
    }

    if (enb_cc_idx != userinfo.user_cfg.supported_cc_list[0].enb_cc_idx) {
      // only check for RAR/Msg3 presence for a UE's PCell
      continue;
    }

    // No RAR allocations outside of rar_window
    uint32_t                prach_tti      = (uint32_t)userinfo.prach_tti;
    uint32_t                primary_cc_idx = userinfo.user_cfg.supported_cc_list[0].enb_cc_idx;
    std::array<uint32_t, 2> rar_window = {prach_tti + 3, prach_tti + 3 + cell_params[primary_cc_idx].prach_rar_window};

    CONDERROR(userinfo.rar_tti < 0 and tti_params.tti_tx_dl > rar_window[1],
              "[TESTER] RAR not scheduled within the RAR Window\n");
    if (tti_params.tti_tx_dl <= rar_window[1] and tti_params.tti_tx_dl >= rar_window[0]) {
      // Inside RAR window
      for (uint32_t i = 0; i < dl_result.nof_rar_elems; ++i) {
        for (uint32_t j = 0; j < dl_result.rar[i].nof_grants; ++j) {
          auto& data = dl_result.rar[i].msg3_grant[j].data;
          if (data.prach_tti == (uint32_t)userinfo.prach_tti and data.preamble_idx == userinfo.preamble_idx) {
            CONDERROR(userinfo.rar_tti >= 0, "There was more than one RAR for the same user\n");
            userinfo.rar_tti = tti_params.tti_tx_dl;
          }
        }
      }
    }

    // Check whether Msg3 was allocated in expected TTI
    if (userinfo.rar_tti >= 0) {
      uint32_t expected_msg3_tti = (uint32_t)(userinfo.rar_tti + FDD_HARQ_DELAY_MS + MSG3_DELAY_MS) % 10240;
      if (expected_msg3_tti == tti_params.tti_tx_ul) {
        for (uint32_t i = 0; i < ul_result.nof_dci_elems; ++i) {
          if (ul_result.pusch[i].dci.rnti == rnti) {
            CONDERROR(userinfo.msg3_tti >= 0, "[TESTER] Only one Msg3 allowed per user\n");
            CONDERROR(ul_result.pusch[i].needs_pdcch, "[TESTER] Msg3 allocations do not require PDCCH\n");
            //              CONDERROR(tti_data.ul_pending_msg3.rnti != rnti, "[TESTER] The UL pending msg3 RNTI did not
            //              match\n"); CONDERROR(not tti_data.ul_pending_msg3_present, "[TESTER] The UL pending msg3
            //              RNTI did not match\n");
            userinfo.msg3_tti = tti_params.tti_tx_ul;
            msg3_count++;
          }
        }
      } else if (expected_msg3_tti < tti_params.tti_tx_ul) {
        CONDERROR(userinfo.msg3_tti < 0, "[TESTER] No UL msg3 allocation was made\n");
      }
    }

    // Find any Msg4 Allocation
    if (userinfo.msg4_tti < 0) {
      for (uint32_t i = 0; i < dl_result.nof_data_elems; ++i) {
        if (dl_result.data[i].dci.rnti == rnti) {
          for (uint32_t j = 0; j < dl_result.data[i].nof_pdu_elems[0]; ++j) {
            if (dl_result.data[i].pdu[0][j].lcid == srslte::sch_subh::CON_RES_ID) {
              // ConRes found
              CONDERROR(dl_result.data[i].dci.format != SRSLTE_DCI_FORMAT1, "ConRes must be format1\n");
              CONDERROR(userinfo.msg4_tti >= 0, "ConRes CE cannot be retransmitted for the same rnti\n");
              userinfo.msg4_tti = tti_params.tti_tx_dl;
            }
          }
          CONDERROR(userinfo.msg4_tti < 0, "Data allocations are not allowed without first receiving ConRes\n");
        }
      }
    }
  }

  return SRSLTE_SUCCESS;
}

int user_state_sched_tester::test_ctrl_info(uint32_t                               enb_cc_idx,
                                            const sched_interface::dl_sched_res_t& dl_result,
                                            const sched_interface::ul_sched_res_t& ul_result)
{
  /* TEST: Ensure there are no spurious RARs that do not belong to any user */
  for (uint32_t i = 0; i < dl_result.nof_rar_elems; ++i) {
    for (uint32_t j = 0; j < dl_result.rar[i].nof_grants; ++j) {
      uint32_t prach_tti    = dl_result.rar[i].msg3_grant[j].data.prach_tti;
      uint32_t preamble_idx = dl_result.rar[i].msg3_grant[j].data.preamble_idx;
      auto     it           = std::find_if(users.begin(), users.end(), [&](const std::pair<uint16_t, ue_state>& u) {
        return u.second.preamble_idx == preamble_idx and ((uint32_t)u.second.prach_tti == prach_tti);
      });
      CONDERROR(it == users.end(), "There was a RAR allocation with no associated user");
      CONDERROR(it->second.user_cfg.supported_cc_list[0].enb_cc_idx != enb_cc_idx,
                "The allocated RAR is in the wrong cc\n");
    }
  }

  /* TEST: All DL allocs have a correct rnti */
  std::set<uint16_t> alloc_rntis;
  for (uint32_t i = 0; i < dl_result.nof_data_elems; ++i) {
    uint16_t rnti = dl_result.data[i].dci.rnti;
    CONDERROR(alloc_rntis.count(rnti) > 0, "The user rnti=0x%x got allocated multiple times in DL\n", rnti);
    CONDERROR(users.count(rnti) == 0, "The user rnti=0x%x allocated in DL does not exist\n", rnti);
    alloc_rntis.insert(rnti);
  }

  /* TEST: All UL allocs have a correct rnti */
  alloc_rntis.clear();
  for (uint32_t i = 0; i < ul_result.nof_dci_elems; ++i) {
    uint16_t rnti = ul_result.pusch[i].dci.rnti;
    CONDERROR(alloc_rntis.count(rnti) > 0, "The user rnti=0x%x got allocated multiple times in UL\n", rnti);
    CONDERROR(users.count(rnti) == 0, "The user rnti=0x%x allocated in UL does not exist\n", rnti);
    alloc_rntis.insert(rnti);
  }

  return SRSLTE_SUCCESS;
}

/**
 * Tests whether the SCells are correctly activated. Individual tests:
 * - no DL and UL allocations in inactive carriers
 */
int user_state_sched_tester::test_scell_activation(uint32_t                               enb_cc_idx,
                                                   const sched_interface::dl_sched_res_t& dl_result,
                                                   const sched_interface::ul_sched_res_t& ul_result)
{
  for (auto& iter : users) {
    uint16_t  rnti     = iter.first;
    ue_state& userinfo = iter.second;

    auto it = std::find_if(userinfo.user_cfg.supported_cc_list.begin(),
                           userinfo.user_cfg.supported_cc_list.end(),
                           [enb_cc_idx](const sched::ue_cfg_t::cc_cfg_t& cc) { return cc.enb_cc_idx == enb_cc_idx; });

    if (it == userinfo.user_cfg.supported_cc_list.end() or not it->active) {
      // cell not active. Ensure data allocations are not made
      for (uint32_t i = 0; i < dl_result.nof_data_elems; ++i) {
        CONDERROR(dl_result.data[i].dci.rnti == rnti, "Allocated user in inactive carrier\n");
      }
      for (uint32_t i = 0; i < ul_result.nof_dci_elems; ++i) {
        CONDERROR(ul_result.pusch[i].dci.rnti == rnti, "Allocated user in inactive carrier\n");
      }
    }
  }

  return SRSLTE_SUCCESS;
}

int user_state_sched_tester::test_all(uint32_t                               enb_cc_idx,
                                      const sched_interface::dl_sched_res_t& dl_result,
                                      const sched_interface::ul_sched_res_t& ul_result)
{
  TESTASSERT(test_ra(enb_cc_idx, dl_result, ul_result) == SRSLTE_SUCCESS);
  TESTASSERT(test_ctrl_info(enb_cc_idx, dl_result, ul_result) == SRSLTE_SUCCESS);
  TESTASSERT(test_scell_activation(enb_cc_idx, dl_result, ul_result) == SRSLTE_SUCCESS);
  return SRSLTE_SUCCESS;
}

void sched_result_stats::process_results(const tti_params_t&                                 tti_params,
                                         const std::vector<sched_interface::dl_sched_res_t>& dl_result,
                                         const std::vector<sched_interface::ul_sched_res_t>& ul_result)
{
  for (uint32_t ccidx = 0; ccidx < dl_result.size(); ++ccidx) {
    for (uint32_t i = 0; i < dl_result[ccidx].nof_data_elems; ++i) {
      user_stats* user = get_user(dl_result[ccidx].data[i].dci.rnti);
      user->tot_dl_sched_data[ccidx] += dl_result[ccidx].data[i].tbs[0];
      user->tot_dl_sched_data[ccidx] += dl_result[ccidx].data[i].tbs[1];
    }
    for (uint32_t i = 0; i < ul_result[ccidx].nof_dci_elems; ++i) {
      user_stats* user = get_user(ul_result[ccidx].pusch[i].dci.rnti);
      user->tot_ul_sched_data[ccidx] += ul_result[ccidx].pusch[i].tbs;
    }
  }
}

sched_result_stats::user_stats* sched_result_stats::get_user(uint16_t rnti)
{
  if (users.count(rnti) != 0) {
    return &users[rnti];
  }
  users[rnti].rnti = rnti;
  users[rnti].tot_dl_sched_data.resize(cell_params.size(), 0);
  users[rnti].tot_ul_sched_data.resize(cell_params.size(), 0);
  return &users[rnti];
}

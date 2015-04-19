  /**
 *
 * \section COPYRIGHT
 *
 * Copyright 2013-2014 The srsLTE Developers. See the
 * COPYRIGHT file at the top-level directory of this distribution.
 *
 * \section LICENSE
 *
 * This file is part of the srsLTE library.
 *
 * srsLTE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * srsLTE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * A copy of the GNU Lesser General Public License can be found in
 * the LICENSE file in the top-level directory of this distribution
 * and at http://www.gnu.org/licenses/.
 *
 */

#include "srslte/srslte.h"
#include "queue.h"

#ifndef UESCHEDGRANT_H
#define UESCHEDGRANT_H

namespace srslte {
namespace ue {  

  /* Uplink/Downlink scheduling grant generated by a successfully decoded PDCCH */ 
  class SRSLTE_API sched_grant {
  public:

    typedef enum {DOWNLINK=0, UPLINK=1} direction_t; 
             sched_grant(direction_t direction, uint16_t rnti);
    uint16_t get_rnti();
    uint32_t get_rv();
    void     set_rv(uint32_t rv);
    bool     get_ndi();
    bool     get_cqi_request();
    int      get_harq_process();    
    bool     is_uplink();
    bool     is_downlink();
    void*    get_grant_ptr();
    void     set_ncce(uint32_t ncce);
    uint32_t get_ncce();
    uint32_t get_tbs(); 
    uint32_t get_current_tx_nb();
    void     set_current_tx_nb(uint32_t current_tx_nb);
  protected: 
    union {
      srslte_ra_pusch_t ul_grant;
      srslte_ra_pdsch_t dl_grant;
    }; 
    uint32_t            current_tx_nb; 
    direction_t         dir; 
    uint16_t            rnti; 
    uint32_t            ncce; 
  };
 
}
}

#endif

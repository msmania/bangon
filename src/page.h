#pragma once

union PML4Entry {
  address_t raw;
  struct {
    address_t p : 1;
    address_t rw : 1;
    address_t us : 1;
    address_t pwt : 1;
    address_t pcd : 1;
    address_t a : 1;
    address_t ignored1 : 1;
    address_t ps : 1; // reserved
    address_t ignored2 : 4;

    address_t to_pdpt : 40;

    address_t ignored3 : 11;
    address_t xd : 1;
  };
};

union PDPTEntry1GB {
  address_t raw;
  struct {
    address_t p : 1;
    address_t rw : 1;
    address_t us : 1;
    address_t pwt : 1;
    address_t pcd : 1;
    address_t a : 1;
    address_t d : 1;
    address_t ps : 1;
    address_t g : 1;
    address_t ignored1 : 3;
    address_t pat : 1;
    address_t reserved : 17;

    address_t to_page : 22;

    address_t ignored2 : 7;
    address_t protection_key : 4;
    address_t xd : 1;
  };
};

union PDPTEntry {
  address_t raw;
  struct {
    address_t p : 1;
    address_t rw : 1;
    address_t us : 1;
    address_t pwt : 1;
    address_t pcd : 1;
    address_t a : 1;
    address_t ignored1 : 1;
    address_t ps : 1;
    address_t ignored2 : 4;

    address_t to_pd : 40;

    address_t ignored3 : 11;
    address_t xd : 1;
  };
};

union PDEntry2MB {
  address_t raw;
  struct {
    address_t p : 1;
    address_t rw : 1;
    address_t us : 1;
    address_t pwt : 1;
    address_t pcd : 1;
    address_t a : 1;
    address_t d : 1;
    address_t ps : 1;
    address_t g : 1;
    address_t ignored1 : 3;
    address_t pat : 1;
    address_t reserved : 8;

    address_t to_page : 31;

    address_t ignored2 : 7;
    address_t protection_key : 4;
    address_t xd : 1;
  };
};

union PDEntry4MB {
  address_t raw;
  struct {
    address_t p : 1;
    address_t rw : 1;
    address_t us : 1;
    address_t pwt : 1;
    address_t pcd : 1;
    address_t a : 1;
    address_t d : 1;
    address_t ps : 1;
    address_t g : 1;
    address_t ignored : 3;
    address_t pat : 1;
    address_t page_high : 8;
    address_t reserved : 1;
    address_t to_page : 10;
    address_t unused : 32;
  };
};

union PDEntry {
  address_t raw;
  struct {
    address_t p : 1;
    address_t rw : 1;
    address_t us : 1;
    address_t pwt : 1;
    address_t pcd : 1;
    address_t a : 1;
    address_t ignored1 : 1;
    address_t ps : 1;
    address_t ignored2 : 4;

    address_t to_pt : 40;

    address_t ignored3 : 11;
    address_t xd : 1;
  };
};

union PTEntry {
  address_t raw;
  struct {
    address_t p : 1;
    address_t rw : 1;
    address_t us : 1;
    address_t pwt : 1;
    address_t pcd : 1;
    address_t a : 1;
    address_t d : 1;
    address_t pat : 1;
    address_t g : 1;
    address_t ignored1 : 3;

    address_t to_page : 40;

    address_t ignored2 : 7;
    address_t protection_key : 4;
    address_t xd : 1;
  };
};

//:
// \file
// \executable to create probability map and evaluate ROI from volm_matcher scores
// Given a result (score) for test query image, generate paorbability map for given tiles
// and report the score for the ground truth loc, associated with its best camera
// \author Yi Dong
// \date Feb 18, 2013

#include <volm/volm_io.h>
#include <volm/volm_tile.h>
#include <volm/volm_camera_space.h>
#include <volm/volm_camera_space_sptr.h>
#include <vul/vul_arg.h>
#include <vul/vul_file.h>
#include <volm/volm_geo_index.h>
#include <volm/volm_geo_index_sptr.h>
#include <volm/volm_loc_hyp.h>
#include <volm/volm_loc_hyp_sptr.h>
#include <vil/vil_save.h>
#include <vil/vil_load.h>
#include <vcl_set.h>
#include <vcl_iomanip.h>
#include <bkml_write.h>

// generate gt hypos
int main(int argc,  char** argv)
{
  vul_arg<vcl_string> cam_bin("-cam", "camera space which is used to give the camera which provides maximum score");
  vul_arg<vcl_string> img("-img", "query images, to get the image size", "");
  vul_arg<vcl_string> gt_file("-gt_locs", "file with the gt locs of all test cases", "");
  vul_arg<vcl_string> geo_hypo_folder("-hypo", "folder to read the geo hypotheses","");
  vul_arg<unsigned> zone_id("-zone", "zone_id", 0);
  vul_arg<vcl_string> candidate_list("-cand", "candidate list if exist", "");
  vul_arg<vcl_string> out("-out", "job output folder", "");
  vul_arg<unsigned> id("-id", "id of the test image", 6);
  vul_arg<unsigned> pass_id("-pass", "from pass 0 to pass 1", 1);
  vul_arg_parse(argc, argv);

  vcl_cout << "argc: " << argc << vcl_endl;
  vcl_stringstream log;
  vcl_string log_file = out() + "/create_prob_map_log.xml";
  if (out().compare("") == 0 ||
      geo_hypo_folder().compare("") == 0 ||
      gt_file().compare("") == 0 ||
      cam_bin().compare("") == 0 ||
      img().compare("") == 0 ||
      pass_id() > 2 ||
      zone_id() == 0 ||
      id() > 100)
  {
    log << "EXE_ARGUMENT_ERROR!\n";
    vul_arg_display_usage_and_exit();
    volm_io::write_post_processing_log(log_file, log.str());
    vcl_cerr << log.str();
    return volm_io::EXE_ARGUMENT_ERROR;
  }

  // read gt location, i.e., lat and lon
  if (!vul_file::exists(gt_file())) {
    log << "ERROR : can not find ground truth position file -->" << gt_file() << '\n';
    volm_io::write_post_processing_log(log_file, log.str());
    vcl_cerr << log.str();
    return volm_io::EXE_ARGUMENT_ERROR;
  }
  vcl_vector<vcl_pair<vgl_point_3d<double>, vcl_pair<vcl_string, vcl_string> > > samples;
  unsigned int cnt = volm_io::read_gt_file(gt_file(), samples);
  if (id() >= cnt) {
    log << "ERROR: gt_file " << gt_file() << " does not contain test id: " << id() << "!\n";
    volm_io::write_post_processing_log(log_file, log.str());
    vcl_cerr << log.str();
    return volm_io::EXE_ARGUMENT_ERROR;
  }

  // check whether we have candidate list for this query
  bool is_candidate = false;
  vgl_polygon<double> cand_poly;
  if ( candidate_list().compare("") != 0) {
    vcl_cout << " candidate list = " <<  candidate_list() << vcl_endl;
    if ( vul_file::extension(candidate_list()).compare(".txt") == 0) {
      is_candidate = true;
      volm_io::read_polygons(candidate_list(), cand_poly);
    }
    else {
      log << " ERROR: candidate list exist but with wrong format, only txt allowed" << candidate_list() << '\n';
      volm_io::write_post_processing_log(log_file, log.str());
      vcl_cerr << log.str();
      return volm_io::EXE_ARGUMENT_ERROR;
    }
  }

  // create tiles
  vcl_vector<volm_tile> tiles;
  if (samples[id()].second.second == "desert")
    tiles = volm_tile::generate_p1_wr1_tiles();
  else
    tiles = volm_tile::generate_p1_wr2_tiles();

  // initialize the Prob_map image if the prob_map doesn't exist
  // if the image exists, load the image instead
  vcl_vector<vil_image_view<float> > tile_imgs;
  for (unsigned i = 0 ; i < tiles.size(); i++) {
    vcl_string img_name = out() + "/" + "ProbMap_" + tiles[i].get_string() + ".tif";
    if (vul_file::exists(img_name)) {
      // load the image
      vil_image_view<float> out_img = vil_load(img_name.c_str());
      tile_imgs.push_back(out_img);
    }
    else {
      // create the image
      vil_image_view<float> out_img(3601, 3601);
      out_img.fill(-1.0f);
      tile_imgs.push_back(out_img);
    }
  }

  // look for the location and camera which provides max_score
  vgl_point_3d<double> max_score_loc;
  unsigned max_score_cam_id;
  float max_score = 0.0f;

  for (unsigned i = 0; i < tiles.size(); i++) {
    volm_tile tile = tiles[i];
    // read in the volm_geo_index for tile i
    vcl_stringstream file_name_pre;
    file_name_pre << geo_hypo_folder() << "geo_index_tile_" << i;
    // no index for tile i exists, continue
    if (!vul_file::exists(file_name_pre.str() + ".txt")) {
      continue;
    }
    // check the zone and tile_id
    if (zone_id() == 17 && i >= 8)
      continue;
    else if (zone_id() == 18 && i < 8)
      continue;
    else if (zone_id()  == 11 && i > 4)
      continue;

    float min_size;
    volm_geo_index_node_sptr root = volm_geo_index::read_and_construct(file_name_pre.str() + ".txt", min_size);
    volm_geo_index::read_hyps(root, file_name_pre.str());
    if (is_candidate) {
      volm_geo_index::prune_tree(root, cand_poly);
    }
    vcl_vector<volm_geo_index_node_sptr> leaves;
    volm_geo_index::get_leaves_with_hyps(root, leaves);

    // load score binary from output folder if exists
    vcl_stringstream score_file;
    score_file << out() << "ps_1_scores_zone_" << zone_id() << "_tile_" << i << ".bin";
    // continue if no score binary exists for this tile
    if (!vul_file::exists(score_file.str()))
      continue;
    vcl_vector<volm_score_sptr> scores;
    volm_score::read_scores(scores, score_file.str());
    // refill the image
    unsigned total_ind = scores.size();
    for (unsigned ii = 0; ii < total_ind; ii++) {
      vgl_point_3d<double> h_pt = leaves[scores[ii]->leaf_id_]->hyps_->locs_[scores[ii]->hypo_id_];

      // look for location and camera giving maximum score
      if (scores[ii]->max_score_ > max_score) {
        max_score = scores[ii]->max_score_;
        max_score_cam_id = scores[ii]->max_cam_id_;
        max_score_loc = h_pt;
      }
#if 0
      vcl_cout << " total_ind = " << total_ind << " ii = " << ii << " leaf_id = " << scores[ii]->leaf_id_ << ", hypo_id = " << scores[ii]->hypo_id_
               << vcl_setprecision(10) << " lon = " << h_pt.x() << " , lat = " << h_pt.y() << ", score = " << scores[ii]->max_score_ << vcl_endl;
#endif
      unsigned u, v;
      if (tile.global_to_img(h_pt.x(), h_pt.y(), u, v)) {
        if (u < tile.ni() && v < tile.nj()) {
          // check if this is the highest values for this pixel
          if (scores[ii]->max_score_ > tile_imgs[i](u,v))
            tile_imgs[i](u,v) = scores[ii]->max_score_;
        }
      }
    }
  } // end of tiles


  // save the location and camera which give maximum score
  if (!vul_file::exists(cam_bin())) {
    log << "ERROR: can not find camera_space binary: " << cam_bin() << '\n';
    volm_io::write_post_processing_log(log_file, log.str());
    vcl_cerr << log.str();
    return volm_io::EXE_ARGUMENT_ERROR;
  }
  vsl_b_ifstream cam_ifs(cam_bin());
  volm_camera_space_sptr cam_space = new volm_camera_space();
  cam_space->b_read(cam_ifs);
  cam_ifs.close();
  if (!vul_file::exists(img())) {
    log << "ERROR: can not find the test query image: " << img() << '\n';
    volm_io::write_post_processing_log(log_file, log.str());
    return volm_io::EXE_ARGUMENT_ERROR;
  }
  vil_image_view<vxl_byte> query_img = vil_load(img().c_str());

  cam_angles max_cam_ang = cam_space->camera_angles(max_score_cam_id);

  vcl_string cam_kml = out() + "/MaxScoreCamera.kml";
  vcl_ofstream ofs_kml(cam_kml.c_str());

  vcl_stringstream kml_name;
  kml_name << "p1a_test1_" << id();
  bkml_write::open_document(ofs_kml);

  double head = (max_cam_ang.heading_ < 0) ? max_cam_ang.heading_ + 360.0 : max_cam_ang.heading_;
  double tilt = (max_cam_ang.tilt_ < 0) ? max_cam_ang.tilt_ + 360 : max_cam_ang.tilt_;
  double roll;
  if (max_cam_ang.roll_ * max_cam_ang.roll_ < 1E-10) roll = 0;
  else                                               roll = max_cam_ang.roll_;
  double tfov = max_cam_ang.top_fov_;
  double tv_rad = tfov / vnl_math::deg_per_rad;
  double ttr = vcl_tan(tv_rad);
  double rfov = vcl_atan( query_img.ni() * ttr / query_img.nj() ) * vnl_math::deg_per_rad;

  bkml_write::write_photo_overlay(ofs_kml, kml_name.str(), max_score_loc.x(), max_score_loc.y(), cam_space->altitude(),
                                  head, tilt, roll, tfov, rfov);
  bkml_write::close_document(ofs_kml);
  ofs_kml.close();

  // save the ProbMap image and output the grount truth score
  for (unsigned i = 0; i < tiles.size(); i++) {
    vcl_string img_name = out() + "/" + "ProbMap_" + tiles[i].get_string() + ".tif";
    vil_save(tile_imgs[i],img_name.c_str());
    unsigned u, v;
    if (tiles[i].global_to_img(samples[id()].first.x(), samples[id()].first.y(), u, v) ) {
      if (u < tiles[i].ni() && v < tiles[i].nj()) {
        log << "\t GT location: " << id() << ", "
            << samples[id()].first.x() << ", "
            << samples[id()].first.y() << " is at pixel: "
            << u << ", " << v << " in tile " << i << " and has value: "
            << tile_imgs[i](u, v)
            << " max score for this test_img = " << max_score
            << " given by camera " << max_cam_ang.get_string()
            << " at location " << max_score_loc.x() << ", " << max_score_loc.y()
            << '\n';
        volm_io::write_post_processing_log(log_file, log.str());
        vcl_cout << log.str();
      }
    }
  }

  return volm_io::SUCCESS;
}

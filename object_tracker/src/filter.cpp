#include <object_tracker/filter.h>

namespace TeamKR
{

Filter::Filter(const std::string& mode)
{
	mode_ = mode;
	if (mode == "car")
	{
		speedLimit_ = CAR_SPEED_LIMIT;
		initTime_ = CAR_FILTER_INIT_TIME;
		resetTime_ = CAR_FILTER_RESET_TIME;
	}
	else if (mode == "ped")
	{
		speedLimit_ = PEDESTRIAN_SPEED_LIMIT;
		initTime_ = PEDESTRIAN_FILTER_INIT_TIME;
		resetTime_ = PEDESTRIAN_FILTER_RESET_TIME;
	}

	initStartTime_ = -1;
	resetStartTime_ = -1;
	initialized_ = false;
	valid_ = false;

	prevTime_ = 0.0;
	prevVel_ << 0, 0;
}

Filter::~Filter()
{

}

void Filter::filterBySize(const std::vector<Cluster>& input, std::vector<Cluster>& output) const
{
	std::list<Cluster>::const_iterator cit = input.begin();
	for (; cit != input.end(); ++cit)
	{
		value_type top = cit->max()(2);
		value_type base = cit->min()(2);
		value_type depth = top - base;
		value_type maxWidth = std::max(cit->max()(0) - cit->min()(0), cit->max()(1) - cit->min()(1));
		if (mode_ == "car")
		{
			if (maxWidth < PEDESTRIAN_MAX_WIDTH || maxWidth > CAR_MAX_WIDTH
				|| top < GROUND_Z + CAR_MIN_DEPTH || top > GROUND_Z + CAR_MAX_DEPTH)
			{
				continue;
			}
			else if (cit->pointCount() < CAR_MIN_POINT_COUNT)
			{
				continue;
			}
			else if (cit->area() > CAR_MAX_AREA)
			{
				continue;
			}
		}
		else if (mode_ == "ped")
		{
			if (maxWidth > PEDESTRIAN_MAX_WIDTH 
				|| top < GROUND_Z + PEDESTRIAN_MIN_DEPTH || top > GROUND_Z + PEDESTRIAN_MAX_DEPTH)
			{
				continue;
			}
			else if (base > PEDESTRIAN_MAX_BASE)
			{
				continue;
			}
			else if (cit->pointCount() < PEDESTRIAN_MIN_POINT_COUNT)
			{
				continue;
			}
			else if (cit->area() > PEDESTRIAN_MAX_AREA)
			{
				continue;
			}
		}

		output.push_back(*cit);
	}
}

void Filter::filterByVelocity(const std::vector<Cluster>& input, int tsSec, int tsNsec, std::vector<Box>& output)
{
	double time = toTime(tsSec, tsNsec);

	if (input.empty())
	{
		// predict
		if (!initialized_)
		{
			// do nothing for now
		}
		else
		{
			Box box(prevBox_);
			Vector2 dp = prevVel_ * (time - prevTime_);
			box.px += dp(0);
			box.py += dp(1);
			output.push_back(box);
		}
	}
	else // size filtered cluster exists
	{
		if (!initialized_ || !valid_)
		{
			// start init timer
			if (initStartTime_ < 0)
			{
				// save current info
				Cluster current;
				if (input.size() == 1)
				{
					current = input.front();
				}
				else
				{
					current = selectCluster(input);
				}
				Box box = toBox(current);
				output.push_back(box);

				prevVel_ << 0, 0;
				prevTime_ = time;
				prevBox_ = box;

				initStartTime_ = time;
			}
			// init timer running
			else
			{
				// filter with velocity
				std::vector<Cluster> found;
				for (std::vector<Cluster>::const_iterator it = input.begin(); it != input.end(); ++it)
				{
					Vector2 point(it->center().x, it->center().y);
					Vector2 vel = velocity(point, time);
					if (vel.norm() < speedLimit_)
					{
						found.push_back(*it);
					}
				}

				if (found.empty())
				{
					// reset init timer
					initStartTime_ = -1;

					// predict
					if (!initialized_)
					{
						// do nothing for now
					}
					else
					{
						Box box(prevBox_);
						Vector2 dp = prevVel_ * (time - prevTime_);
						box.px += dp(0);
						box.py += dp(1);
						output.push_back(box);
					}
				}
				else // found
				{
					// save current info
					Cluster current;
					if (found.size() == 1)
					{
						current = found.front();
					}
					else
					{
						current = selectCluster(found);
					}
					Box box = toBox(current);
					output.push_back(box);

					Vector2 point(current.center().x, current.center().y);
					Vector2 vel = velocity(point, time);
					prevVel_ = vel;
					prevTime_ = time;
					prevBox_ = box;

					// if init condition satisfied 
					if (time - initStartTime_ > initTime_)
					{
						initialized_ = true;
						valid_ = true;
						initStartTime_ = -1;
					}
				}
			}
		}
		else // initialized and valid
		{
			// filter with velocity
			std::vector<Cluster> found;
			for (std::vector<Cluster>::const_iterator it = input.begin(); it != input.end(); ++it)
			{
				Vector2 point(it->center().x, it->center().y);
				Vector2 vel = velocity(point, time);
				if (vel.norm() < speedLimit_)
				{
					found.push_back(*it);
				}
			}

			if (found.empty())
			{
				// predict
				Box box(prevBox_);
				Vector2 dp = prevVel_ * (time - prevTime_);
				box.px += dp(0);
				box.py += dp(1);
				output.push_back(box);

				// start reset timer
				if (resetStartTime_ < 0)
				{
					resetStartTime_ = time;
				}
				// reset timer running
				else if (time - resetStartTime_ > resetTime_)
				{
					valid_ = false;
					resetStartTime_ = -1;
					// start initialization
					std::vector<Cluster> dummy;
					filterByVelocity(input, tsSec, tsNsec, dummy);
				}
			}
			else // found
			{
				// save current info
				Cluster current;
				if (found.size() == 1)
				{
					current = found.front();
				}
				else
				{
					current = selectCluster(found);
				}
				Box box = toBox(current);
				output.push_back(box);
				Vector2 point(current.center().x, current.center().y);
				Vector2 vel = velocity(point, time);
				prevVel_ = vel;
				prevTime_ = time;
				prevBox_ = box;

				initStartTime_ = -1;
				resetStartTime_ = -1;
			}
		}
	}
}

double Filter::toTime(int tsSec, int tsNsec) const
{
	return 1E-9 * (double)tsNsec + (double)tsSec;
}

Vector2 Filter::velocity(const Vector2& pos, double time) const
{
	Vector2 prevPos(prevCluster_.center().x, prevCluster_.center().y);
	return (pos - prevPos) / (time - prevTime_);
}

Box Filter::toBox(const Cluster& cluster) const
{
	Box box;
	Vector3 center = cluster.center();
	Vector3 min = cluster.min();
	Vector3 max = cluster.max();

	box.px = center(0);
	box.py = center(1);
	box.pz = center(2);
	box.width = max(0) - min(0);
	box.height = max(1) - min(1);
	box.depth = max(2) - min(2);
}

const Cluster& Filter::selectCluster(const std::vector<Cluster>& input)
{
	std::vector<Cluster>::const_iterator it = input.begin();
	Cluster cluster = *it;
	++it;
	for (; it != input.end(); ++it)
	{
		if (it->pointCount() > cluster.pointCount())
		{
			cluster = *it;
		}
	}

	return cluster;
}

}

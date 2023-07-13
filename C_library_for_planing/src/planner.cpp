#include "planner.h"
#include "robot.h"
#include <assert.h>
#include <random>
#include <vector>
#include <deque>
#include <iostream>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <iterator>
#include <map>
#include <stdexcept>
#include <assert.h>

// из-за числовой нестабильности чисел с плавающей запятой
// Возможны случаи, когда косинус равен не 1, а 1.000001 и т. д.
#define COS_TOLERANCE 0.00000001

void RRT::Tree::Node::add_childen(std::shared_ptr<Node> child)
{
    assert(!this->is_children(child));
    assert(child); // проверка на nullptr
    children.insert(child);
    child->set_parent(weak_from_this());
}

void RRT::Tree::Node::set_parent(std::weak_ptr<Node> parent)
{
    _parent = parent;
}

bool RRT::Tree::Node::is_children(std::shared_ptr<Node> child)
{
    return children.count(child);
}

double RRT::Tree::Node::distance(std::shared_ptr<Node> other)
{
    std::vector<double> n1 = get_position().configuration;
    std::vector<double> n2 = (*other).get_position().configuration;
    double result = 0;
    for (int i = 0; i < n1.size(); i++)
    {
        result += (n1[i] - n2[i]) * (n1[i] - n2[i]);
    }
    return result;
}

double RRT::Tree::Node::distance(Robot other)
{
    assert(this->get_position().dof_ == other.dof_);
    std::vector<double> n1 = this->get_position().configuration;
    std::vector<double> n2 = other.configuration;
    assert(n1.size() == n2.size());

    double result = 0;
    for (int i = 0; i < n1.size(); i++)
    {
        assert(!std::isnan(n1[i]));
        assert(!std::isnan(n2[i]));

        result += (n1[i] - n2[i]) * (n1[i] - n2[i]);
        assert(!std::isnan(result));
    }
    return std::sqrt(result);
}

std::weak_ptr<RRT::Tree::Node> RRT::Tree::Node::get_parent()
{
    return this->_parent;
}

Robot RRT::Tree::Node::get_position()
{
    return position;
}

void RRT::grow_tree()
{

    if (probability_gen(gen) < goal_bias)
    {
        expand_to_goal();
    }
    else
    {
        expand_to_random();
    }
    save_tree();
    iteration_count++;
}

void RRT::sample_all_goals(std::vector<Robot> &answers, Robot pos, int depth, Vector2D intermediate_goal, double length_koef)
{
    Robot sample = pos;
    double remaining_joint_length = 0;

    for (int joint_count = depth + 1; joint_count < dof - 1; joint_count++)
    {
        remaining_joint_length += sample.joints[joint_count].length;
    }

    if (depth != (dof - 2))
    {
        remaining_joint_length *= length_koef;
    }

    double curr_angle = 0;
    Vector2D last(0, 0);

    for (int joint_count = 0; joint_count < depth; joint_count++)
    {
        curr_angle += sample.configuration[joint_count];
        last.x += sample.joints[joint_count].length * std::cos(curr_angle * 3.1415 / 180);
        last.y += sample.joints[joint_count].length * std::sin(curr_angle * 3.1415 / 180);
    }

    double current_joint_length = sample.joints[depth].length;
    Vector2D curr_vector(current_joint_length * std::cos(curr_angle * 3.1415 / 180), current_joint_length * std::sin(curr_angle * 3.1415 / 180));
    Vector2D vector_to_goal(intermediate_goal.x - last.x, intermediate_goal.y - last.y);
    double dist_to_goal = vector_to_goal.length();

    // std::cout<<depth<<" "<<dist_to_goal<<" "<<current_joint_length<<" "<<remaining_joint_length<<std::endl;
    if ((goal.delta < dist_to_goal - (current_joint_length + remaining_joint_length)) || (dist_to_goal < std::abs(current_joint_length - remaining_joint_length)))
    {
        return;
    }
    if (dist_to_goal == 0)
    {
        sample.configuration[depth] = 0;
    }
    else
    {
        double triangle_angle_cos = -(remaining_joint_length * remaining_joint_length - current_joint_length * current_joint_length - dist_to_goal * dist_to_goal) / (2 * dist_to_goal * current_joint_length);
        double angle_1 = std::acos(triangle_angle_cos);
        if (std::isnan(angle_1))
        {
            if ((std::abs(triangle_angle_cos) - 1) > COS_TOLERANCE)
            {
                return;
            }
            if (triangle_angle_cos > 0)
            {
                angle_1 = 0;
            }
            else
            {
                angle_1 = 180;
            }
        }
        double full_angle_cos = curr_vector.dotProduct(vector_to_goal) / dist_to_goal;

        double angle_2 = std::acos(full_angle_cos) - angle_1;
        if (std::isnan(angle_2))
        {
            if ((std::abs(full_angle_cos) - 1) > COS_TOLERANCE)
            {
                return;
            }
            if (full_angle_cos > 0)
            {
                angle_2 = -angle_1;
            }
            else
            {
                angle_2 = 180 - angle_1;
            }
        }
        int orientation = curr_vector.x * vector_to_goal.y - curr_vector.y * vector_to_goal.x;
        orientation /= std::abs(orientation);
        angle_2 *= orientation;
        angle_2 = angle_2 * 180 / 3.1415;

        sample.configuration[depth] = angle_2;
    }
    if ((sample.configuration[depth]  < sample.joints[depth].limits[0])||(sample.configuration[depth]  > sample.joints[depth].limits[1]))
    {
        return;
    }
    if (depth == (dof - 2))
    {
        sample.configuration[dof - 1] -= curr_angle + sample.configuration[dof - 2];
        if(!collide(sample, sample.configuration, obstacles)){
        answers.push_back(sample);
        }
        return;
    }
    depth++;
    int from = 3;
    if (depth == (dof - 2))
    {
        from = 10;
    }
    for (int i = from; i <= 10; i += 1)
        sample_all_goals(answers, sample, depth, intermediate_goal, i / 10.0);

    sample.configuration[depth - 1] = fix_fmod(-sample.configuration[depth - 1] + 180);
    for (int i = from; i <= 10; i += 1)
        sample_all_goals(answers, sample, depth, intermediate_goal, i / 10.0);
}

Robot RRT::get_end_config_sample()
{
    if (end_configurations.size())
    {
        return end_configurations[std::floor(end_configurations.size() * probability_gen(gen) * 0.99)];
    }
    throw std::invalid_argument("No solution found for goal configuration.");
}
void RRT::expand_to_goal()
{
    Robot sample_goal_config = get_end_config_sample();
    std::shared_ptr<RRT::Tree::Node> nearest_node = nearest_neighbour(sample_goal_config);
    std::shared_ptr<RRT::Tree::Node> next_node = nearest_node;
    do
    {
        nearest_node = next_node;
        next_node = make_step(nearest_node, sample_goal_config);
        if (next_node)
        {
            nearest_node->add_childen(next_node);
        }
    } while (next_node && (!is_goal(next_node)));
    if (next_node && is_goal(next_node))
    {
        finished = true;
        finish_node.push_back(next_node);
    }
}
void RRT::expand_to_random()
{
    Robot random_position = random_sample();
    std::shared_ptr<RRT::Tree::Node> nearest_node = nearest_neighbour(random_position);
    std::shared_ptr<RRT::Tree::Node> next_node = make_step(nearest_node, random_position);
    if (next_node)
    {
        nearest_node->add_childen(next_node);
        // for (int i = 0; i < dof; i++)
        // {
        //     std::cout << (*next_node).get_position().configuration[i] << ' ';
        // }
        // std::cout << std::endl;
        if (is_goal(next_node))
        {
            finished = true;
            finish_node.push_back(next_node);
        }
    }
}
bool RRT::is_finished() const
{
    return finished;
}

Robot RRT::random_sample()
{
    Robot sample = Robot(start);

    for (int i = 0; i < start.configuration.size(); i++)
    {

        sample.configuration[i] = random_gen[i](gen);
    }
    return sample;
}

std::shared_ptr<RRT::Tree::Node> RRT::nearest_neighbour(const Robot &pos)
{
    // Определение ближайшего соседа полным перебором
    double min_distance = tree.head->distance(pos);
    std::shared_ptr<RRT::Tree::Node> min_node = tree.head;
    std::deque<std::shared_ptr<RRT::Tree::Node>> to_go;
    for (auto child : (*min_node).children)
    {
        to_go.push_back(child);
    }
    while (to_go.size() > 0)
    {
        if (min_distance > (*to_go[0]).distance(pos))
        {
            min_distance = (*to_go[0]).distance(pos);
            min_node = to_go[0];
        }
        for (auto child : to_go[0]->children)
        {
            to_go.push_back(child);
        }
        to_go.pop_front();
    }
    return min_node;
}

std::shared_ptr<RRT::Tree::Node> RRT::make_step(const std::shared_ptr<RRT::Tree::Node> node, const Robot &pos)
{
    // Делаем фиксированный шаг в сторону точки
    //  проверяем на колизион, если есть, то ограничиваем до стенки
    // for (auto angle : pos.configuration)
    // {

    //     assert(!std::isnan(angle));
    // }
    // std::cout << (*node).distance(pos) << std::endl;
    Robot delta;
    if ((*node).distance(pos) != 0)
    {
        delta = (pos - (*node).get_position()) / (*node).distance(pos); // нормированный вектор от ноды по рандомной позиции
    }
    else
    {

            std::shared_ptr<RRT::Tree::Node> null_node;
            return null_node;

    }
    // std::random_device rd;
    // std::mt19937 gen(rd());
    // std::uniform_real_distribution<> dis(0.1, tolerance);
    // for (auto angle : delta.configuration)
    // {

    //     assert(!std::isnan(angle));
    // }
    Robot new_pose = (*node).get_position() + delta * tolerance; // dis(gen);
    if (node->distance(new_pose) > node->distance(pos))
    {
        new_pose = pos;
    }
    if (collide(new_pose, new_pose.configuration, obstacles))
    {
        std::cout<<"aaaaa"<<std::endl;
        std::shared_ptr<RRT::Tree::Node> null_node;
        return null_node;
    }
    std::shared_ptr<RRT::Tree::Node> new_node(new RRT::Tree::Node(new_pose));
    return new_node;
}
bool RRT::is_goal(std::shared_ptr<RRT::Tree::Node> node)
{
    Robot pos = (*node).get_position();
    Vector2D q = end_effector(pos, pos.configuration);
    double angle = 0.0;
    for (auto i = 0u; i < pos.dof_; i++)
    {
        angle += pos.configuration[i];
    }
    return goal.is_goal(q.x, q.y, angle);
}
template <typename T>
bool is_uninitialized(std::weak_ptr<T> const &weak)
{
    using wt = std::weak_ptr<T>;
    return !weak.owner_before(wt{}) && !wt{}.owner_before(weak);
}
std::deque<std::vector<double>> RRT::get_path() const
{
    std::deque<std::vector<double>> path;
    std::weak_ptr<RRT::Tree::Node> node = finish_node[0];
    if (std::shared_ptr<RRT::Tree::Node> spt = node.lock())
    {
        while (!is_uninitialized<RRT::Tree::Node>(spt->get_parent()))
        {
            node = spt->get_parent();
            if (std::shared_ptr<RRT::Tree::Node> nspt = node.lock())
            {
                path.push_front(nspt->get_position().configuration);
            }
            spt = node.lock();
        }
    }
    return path;
}

void RRT::save_tree()
{
    std::deque<std::shared_ptr<RRT::Tree::Node>> need_visit;
    std::map<std::shared_ptr<RRT::Tree::Node>, int> node2ind;
    need_visit.push_back(tree.head);
    int node_ind = 0;

    node2ind[tree.head] = 0;
    std::ofstream ofs("tree.csv", std::ofstream::out);
    ofs << 0 << "," << tree.head->get_position().configuration[0] << "," << tree.head->get_position().configuration[1] << "," << 0 << std::endl;
    while (need_visit.size() > 0)
    {
        int curr_parent = node2ind[need_visit[0]];
        for (auto child : (*need_visit[0]).children)
        {
            need_visit.push_back(child);
            node_ind++;
            node2ind[child] = node_ind;
            ofs << node_ind << "," << child->get_position().configuration[0] << "," << child->get_position().configuration[1] << "," << curr_parent << std::endl;
        }
        need_visit.pop_front();
    }
    ofs.close();
}
void RRT::save(const std::string out_filename_csv, const std::string out_filename_xml, const std::string in_filename) const
{
    std::deque<std::vector<double>> path = this->get_path();
    std::ofstream ofs(out_filename_csv, std::ofstream::out);
    std::string path_csv = "";
    for (int i = 0; i < path.size(); i++)
    {
        if (dof == 1)
        {
            ofs << std::to_string(path[i][0]) << '\n';
            path_csv += std::to_string(path[i][0]) + '\n';
        }
        else
        {
            for (int j = 0; j < dof - 1; j++)
            {
                ofs << std::to_string(path[i][j]) << ",";
                path_csv += std::to_string(path[i][j]) + ',';
            }
            ofs << path[i][dof - 1] << '\n';
            path_csv += std::to_string(path[i][dof - 1]) + '\n';
        }
    }
    ofs.close();

    std::ifstream sourceFile(in_filename);
    std::ofstream targetFile(out_filename_xml, std::ofstream::out);
    if (sourceFile.is_open())
    {
        int line_count = std::count(std::istreambuf_iterator<char>(sourceFile),
                                    std::istreambuf_iterator<char>(), '\n');
        sourceFile.seekg(0, std::ios_base::beg);
        // Открываем целевой файл для записи

        if (targetFile.is_open())
        {
            // Копируем содержимое исходного файла в целевой файл
            for (int i = 0; i < line_count; i++)
            {
                std::string line = "";
                std::getline(sourceFile, line);
                targetFile << line << std::endl;
            }
        }

        sourceFile.close();
    }

    if (targetFile.is_open())
    {
        targetFile << "<csv>" << path_csv << "</csv>" << std::endl;
        targetFile << "</input_info>";
        targetFile.close();

        std::cout << "Данные успешно добавлены в файл." << std::endl;
    }
    else
    {
        std::cerr << "Не удалось открыть файл для дозаписи." << std::endl;
    }

    std::cout << "XML file created successfully." << std::endl;
}